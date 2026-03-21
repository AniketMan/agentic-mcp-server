// Handlers_PythonBridge.cpp
// Python bridge with structured execution and output capture.
// UE 5.6 target.
//
// Fixes applied (MCP_REQUESTS.md):
//   #4: Inline Python execution no longer prepends -c. Always writes to temp file.
//   #5: Both handlers now capture stdout/stderr via GLog output device.
//
// Endpoints:
//   pythonExecFile   - Execute a .py file with output capture
//   pythonExecString - Execute inline Python code with output capture

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformProcess.h"

// Output capture device shared by both handlers
class FPyBridgeOutputCapture : public FOutputDevice
{
public:
	FString Stdout;
	FString Stderr;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category == FName("LogPython") || Category == FName("PythonLog"))
		{
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning)
				Stderr += FString(V) + TEXT("\n");
			else
				Stdout += FString(V) + TEXT("\n");
		}
	}
};

// ============================================================================
// pythonExecFile — Execute a .py file with output capture
// Body: { "filePath": "/path/to/script.py", "args": ["arg1", "arg2"] }
// ============================================================================
FString FAgenticMCPServer::HandlePythonExecFile(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'filePath'"));

	// Path sanitization
	FPaths::CollapseRelativeDirectories(FilePath);
	if (FilePath.Contains(TEXT("..")))
		return MakeErrorJson(TEXT("Path traversal detected: '..' not allowed"));

	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString FullFilePath = FPaths::ConvertRelativePathToFull(FilePath);
	if (!FullFilePath.StartsWith(ProjectDir))
		return MakeErrorJson(FString::Printf(TEXT("Path outside project directory: %s"), *FullFilePath));

	if (!FPaths::FileExists(FilePath))
		return MakeErrorJson(FString::Printf(TEXT("File not found: %s"), *FilePath));

	FString Command = FString::Printf(TEXT("py \"%s\""), *FilePath);

	const TArray<TSharedPtr<FJsonValue>>* ArgsArray = nullptr;
	if (Json->TryGetArrayField(TEXT("args"), ArgsArray))
	{
		for (const TSharedPtr<FJsonValue>& Arg : *ArgsArray)
		{
			Command += FString::Printf(TEXT(" \"%s\""), *Arg->AsString());
		}
	}

	// Capture output
	FPyBridgeOutputCapture Capture;
	GLog->AddOutputDevice(&Capture);

	bool bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
	FPlatformProcess::Sleep(0.1f);

	GLog->RemoveOutputDevice(&Capture);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("command"), Command);
	OutJson->SetStringField(TEXT("stdout"), Capture.Stdout.TrimEnd());
	OutJson->SetStringField(TEXT("stderr"), Capture.Stderr.TrimEnd());
	OutJson->SetBoolField(TEXT("hasErrors"), !Capture.Stderr.IsEmpty());
	return JsonToString(OutJson);
}

// ============================================================================
// pythonExecString — Execute inline Python code with output capture
// Body: { "code": "import unreal; print('hello')" }
//
// FIX #4: No longer prepends -c. Always writes to temp file then executes.
// FIX #5: Always captures stdout/stderr and returns them in the response.
// ============================================================================
FString FAgenticMCPServer::HandlePythonExecString(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Code = Json->GetStringField(TEXT("code"));
	if (Code.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'code'"));

	// Always write to unique temp file (fixes #4 — no -c prepend, no syntax errors)
	FString TempPath = FPaths::ProjectSavedDir() / FString::Printf(
		TEXT("MCP_Py_%s.py"), *FGuid::NewGuid().ToString());
	if (!FFileHelper::SaveStringToFile(Code, *TempPath))
		return MakeErrorJson(FString::Printf(TEXT("Failed to write temp file: %s"), *TempPath));

	FString Command = FString::Printf(TEXT("py \"%s\""), *TempPath);

	// Capture output (fixes #5 — always return stdout/stderr)
	FPyBridgeOutputCapture Capture;
	GLog->AddOutputDevice(&Capture);

	bool bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
	FPlatformProcess::Sleep(0.1f);

	GLog->RemoveOutputDevice(&Capture);

	// Clean up temp file
	IFileManager::Get().Delete(*TempPath);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("command"), Command);
	OutJson->SetNumberField(TEXT("codeLength"), Code.Len());
	OutJson->SetStringField(TEXT("stdout"), Capture.Stdout.TrimEnd());
	OutJson->SetStringField(TEXT("stderr"), Capture.Stderr.TrimEnd());
	OutJson->SetBoolField(TEXT("hasErrors"), !Capture.Stderr.IsEmpty());
	return JsonToString(OutJson);
}
