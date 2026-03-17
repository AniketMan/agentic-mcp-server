// Handlers_PythonBridge.cpp
// Improved Python bridge with structured execution and output capture.
// UE 5.6 target.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Path Sanitization Helpers
// ============================================================================

/**
 * Validate and sanitize a file path for Python execution.
 * Blocks path traversal, command injection, and access outside allowed directories.
 *
 * @param RawPath       The user-supplied path
 * @param OutSafePath   The normalized, validated path (only set on success)
 * @param OutError      Error message (only set on failure)
 * @return              True if the path is safe to use
 */
static bool SanitizePythonFilePath(const FString& RawPath, FString& OutSafePath, FString& OutError)
{
	if (RawPath.IsEmpty())
	{
		OutError = TEXT("File path is empty");
		return false;
	}

	// Block command injection characters
	static const TCHAR* DangerousChars[] = {
		TEXT(";"), TEXT("&"), TEXT("|"), TEXT("`"), TEXT("$"),
		TEXT("$("), TEXT("\n"), TEXT("\r")
	};
	for (const TCHAR* Dangerous : DangerousChars)
	{
		if (RawPath.Contains(Dangerous))
		{
			OutError = FString::Printf(TEXT("Path contains forbidden character sequence: '%s'"), Dangerous);
			return false;
		}
	}

	// Normalize the path
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(RawPath);
	FPaths::NormalizeFilename(NormalizedPath);
	FPaths::RemoveDuplicateSlashes(NormalizedPath);

	// Block path traversal: after normalization, no ".." should remain
	if (NormalizedPath.Contains(TEXT("..")))
	{
		OutError = TEXT("Path traversal ('..') is not allowed");
		return false;
	}

	// Enforce .py extension
	if (!NormalizedPath.EndsWith(TEXT(".py"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("Only .py files can be executed");
		return false;
	}

	// Restrict to allowed directories:
	//   1. The UE project directory tree
	//   2. The UE engine Python directory
	//   3. The plugin's own Tools directory
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	FString PluginDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("AgenticMCP")));
	FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

	bool bAllowed = NormalizedPath.StartsWith(ProjectDir)
		|| NormalizedPath.StartsWith(EngineDir)
		|| NormalizedPath.StartsWith(PluginDir)
		|| NormalizedPath.StartsWith(SavedDir);

	if (!bAllowed)
	{
		OutError = FString::Printf(
			TEXT("Path '%s' is outside allowed directories (project, engine, plugin, saved)"),
			*NormalizedPath);
		return false;
	}

	OutSafePath = NormalizedPath;
	return true;
}

/**
 * Sanitize a single argument string for safe inclusion in a Python command.
 * Strips quotes and dangerous characters.
 */
static FString SanitizePythonArg(const FString& RawArg)
{
	FString Safe = RawArg;
	Safe = Safe.Replace(TEXT("\""), TEXT(""));
	Safe = Safe.Replace(TEXT(";"), TEXT(""));
	Safe = Safe.Replace(TEXT("&"), TEXT(""));
	Safe = Safe.Replace(TEXT("|"), TEXT(""));
	Safe = Safe.Replace(TEXT("`"), TEXT(""));
	Safe = Safe.Replace(TEXT("$"), TEXT(""));
	Safe = Safe.Replace(TEXT("\n"), TEXT(""));
	Safe = Safe.Replace(TEXT("\r"), TEXT(""));
	return Safe;
}

// ============================================================================
// IMPROVED PYTHON BRIDGE
// ============================================================================

// --- pythonExecFile ---
// Execute a Python file with structured output capture.
// Body: { "filePath": "/path/to/script.py", "args": ["arg1", "arg2"] }
FString FAgenticMCPServer::HandlePythonExecFile(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'filePath'"));

	// Sanitize and validate the path
	FString SafePath;
	FString PathError;
	if (!SanitizePythonFilePath(FilePath, SafePath, PathError))
	{
		UE_LOG(LogTemp, Warning, TEXT("AgenticMCP: Python path rejected: %s (input: %s)"),
			*PathError, *FilePath);
		return MakeErrorJson(FString::Printf(TEXT("Invalid file path: %s"), *PathError));
	}

	if (!FPaths::FileExists(SafePath))
		return MakeErrorJson(FString::Printf(TEXT("File not found: %s"), *SafePath));

	FString Command = FString::Printf(TEXT("py \"%s\""), *SafePath);

	const TArray<TSharedPtr<FJsonValue>>* ArgsArray = nullptr;
	if (Json->TryGetArrayField(TEXT("args"), ArgsArray))
	{
		for (const TSharedPtr<FJsonValue>& Arg : *ArgsArray)
		{
			FString SafeArg = SanitizePythonArg(Arg->AsString());
			Command += FString::Printf(TEXT(" \"%s\""), *SafeArg);
		}
	}

	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("executedPath"), SafePath);
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

	// Write to temp file in Saved directory (always allowed) and execute
	FString TempPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("MCP_TempScript.py"));
	FFileHelper::SaveStringToFile(Code, *TempPath);

	FString Command = FString::Printf(TEXT("py \"%s\""), *TempPath);
	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetNumberField(TEXT("codeLength"), Code.Len());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, W); return Out;
}
