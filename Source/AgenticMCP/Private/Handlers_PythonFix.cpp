// Handlers_PythonFix.cpp
// Fixed Python execution handler with proper output capture.
// Replaces the fire-and-forget executePython with a version that captures stdout/stderr.
//
// Endpoints:
//   executePythonCapture  - Execute Python script with captured output (replaces executePython)

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/OutputDeviceHelper.h"
#include "HAL/PlatformProcess.h"

// Custom output device to capture log output during Python execution
class FPythonOutputCapture : public FOutputDevice
{
public:
	FString CapturedOutput;
	FString CapturedErrors;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		FString Line = FString(V);
		if (Category == FName("LogPython") || Category == FName("PythonLog"))
		{
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning)
			{
				CapturedErrors += Line + TEXT("\n");
			}
			else
			{
				CapturedOutput += Line + TEXT("\n");
			}
		}
	}
};

// ============================================================
// executePythonCapture - Execute Python with output capture
// ============================================================
FString FAgenticMCPServer::HandleExecutePythonCapture(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Script = Json->HasField(TEXT("script")) ? Json->GetStringField(TEXT("script")) : TEXT("");
	FString File = Json->HasField(TEXT("file")) ? Json->GetStringField(TEXT("file")) : TEXT("");

	if (Script.IsEmpty() && File.IsEmpty())
		return MakeErrorJson(TEXT("Provide either 'script' (inline code) or 'file' (path to .py file)"));

	// Check if Python plugin is available
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
	{
		return MakeErrorJson(TEXT("Python Editor Script Plugin is not loaded. Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin"));
	}

	// Set up output capture
	FPythonOutputCapture OutputCapture;
	GLog->AddOutputDevice(&OutputCapture);

	bool bSuccess = false;
	FString Command;

	if (!File.IsEmpty())
	{
		// Execute a .py file
		if (!FPaths::FileExists(File))
		{
			GLog->RemoveOutputDevice(&OutputCapture);
			return MakeErrorJson(FString::Printf(TEXT("Python file not found: %s"), *File));
		}
		Command = FString::Printf(TEXT("py \"%s\""), *File);
	}
	else
	{
		// Execute inline script
		// For multi-line scripts, write to a temp file
		if (Script.Contains(TEXT("\n")))
		{
			FString TempPath = FPaths::ProjectSavedDir() / TEXT("AgenticMCP_temp.py");
			FFileHelper::SaveStringToFile(Script, *TempPath);
			Command = FString::Printf(TEXT("py \"%s\""), *TempPath);
		}
		else
		{
			Command = FString::Printf(TEXT("py %s"), *Script);
		}
	}

	// Execute
	bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	// Give a moment for log output to flush
	FPlatformProcess::Sleep(0.1f);

	// Remove capture device
	GLog->RemoveOutputDevice(&OutputCapture);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("command"), Command);

	// Trim trailing newlines
	FString Output = OutputCapture.CapturedOutput.TrimEnd();
	FString Errors = OutputCapture.CapturedErrors.TrimEnd();

	OutJson->SetStringField(TEXT("stdout"), Output);
	OutJson->SetStringField(TEXT("stderr"), Errors);

	if (!Errors.IsEmpty())
	{
		OutJson->SetBoolField(TEXT("hasErrors"), true);
	}
	else
	{
		OutJson->SetBoolField(TEXT("hasErrors"), false);
	}

	return JsonToString(OutJson);
}
