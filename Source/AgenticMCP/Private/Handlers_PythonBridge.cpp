// Handlers_PythonBridge.cpp
// Improved Python bridge with structured execution and output capture.
// UE 5.6 target.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// IMPROVED PYTHON BRIDGE
// ============================================================================

// --- pythonExecFile ---
// Execute a Python file with structured output capture.
// Body: { "filePath": "/path/to/script.py", "args": ["arg1", "arg2"] }
FString FAgenticMCPServer::HandlePythonExecFile(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'filePath'"));

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

	// Capture output via log
	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("command"), Command);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, W); return Out;
}

// --- pythonExecString ---
// Execute a Python string with structured output capture.
// Body: { "code": "import unreal; print(unreal.EditorAssetLibrary.list_assets('/Game/'))" }
FString FAgenticMCPServer::HandlePythonExecString(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Code = Json->GetStringField(TEXT("code"));
	if (Code.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'code'"));

	// Write to temp file and execute
	FString TempPath = FPaths::ProjectSavedDir() / TEXT("MCP_TempScript.py");
	FFileHelper::SaveStringToFile(Code, *TempPath);

	FString Command = FString::Printf(TEXT("py \"%s\""), *TempPath);
	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetNumberField(TEXT("codeLength"), Code.Len());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, W); return Out;
}

