// Handlers_Console.cpp
// Console command handlers for AgenticMCP.
// Provides: execute console command, get/set CVars, list CVars

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/Engine.h"
#include "Engine/Console.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"

FString FAgenticMCPServer::HandleExecuteConsole(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	if (!BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Command = BodyJson->GetStringField(TEXT("command"));
	if (Command.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'command' field"));
	}

	// Execute the console command
	if (GEngine)
	{
		GEngine->Exec(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, *Command);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("command"), Command);
	OutJson->SetStringField(TEXT("message"), TEXT("Console command executed"));

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleGetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	if (!BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Name = BodyJson->GetStringField(TEXT("name"));
	if (Name.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' field"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("name"), Name);
	OutJson->SetStringField(TEXT("value"), CVar->GetString());

	// Add type info
	if (CVar->IsVariableInt())
	{
		OutJson->SetStringField(TEXT("type"), TEXT("int"));
		OutJson->SetNumberField(TEXT("intValue"), CVar->GetInt());
	}
	else if (CVar->IsVariableFloat())
	{
		OutJson->SetStringField(TEXT("type"), TEXT("float"));
		OutJson->SetNumberField(TEXT("floatValue"), CVar->GetFloat());
	}
	else if (CVar->IsVariableBool())
	{
		OutJson->SetStringField(TEXT("type"), TEXT("bool"));
		OutJson->SetBoolField(TEXT("boolValue"), CVar->GetBool());
	}
	else
	{
		OutJson->SetStringField(TEXT("type"), TEXT("string"));
	}

	// Get help text
	OutJson->SetStringField(TEXT("help"), CVar->GetHelp());

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleSetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	if (!BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Name = BodyJson->GetStringField(TEXT("name"));
	if (Name.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' field"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	// Get the value to set
	FString OldValue = CVar->GetString();
	FString NewValue;

	if (BodyJson->HasField(TEXT("value")))
	{
		// Try different types
		TSharedPtr<FJsonValue> ValueField = BodyJson->TryGetField(TEXT("value"));
		if (ValueField->Type == EJson::Number)
		{
			NewValue = FString::SanitizeFloat(BodyJson->GetNumberField(TEXT("value")));
		}
		else if (ValueField->Type == EJson::Boolean)
		{
			NewValue = BodyJson->GetBoolField(TEXT("value")) ? TEXT("1") : TEXT("0");
		}
		else
		{
			NewValue = BodyJson->GetStringField(TEXT("value"));
		}
	}
	else
	{
		return MakeErrorJson(TEXT("Missing 'value' field"));
	}

	// Set the value
	CVar->Set(*NewValue, ECVF_SetByConsole);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("name"), Name);
	OutJson->SetStringField(TEXT("oldValue"), OldValue);
	OutJson->SetStringField(TEXT("newValue"), CVar->GetString());

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleListCVars(const TMap<FString, FString>& Params, const FString& Body)
{
	// UE 5.6: IConsoleManager::IsAvailable() removed - console manager is always available
	if (!GEngine)
	{
		return MakeErrorJson(TEXT("Console manager not available"));
	}
	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);

	FString Filter;
	int32 MaxResults = 100;

	if (BodyJson.IsValid())
	{
		Filter = BodyJson->GetStringField(TEXT("filter"));
		if (BodyJson->HasField(TEXT("maxResults")))
		{
			MaxResults = (int32)BodyJson->GetNumberField(TEXT("maxResults"));
		}
	}

	TArray<TSharedPtr<FJsonValue>> CVarArray;
	int32 Count = 0;

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
		FConsoleObjectVisitor::CreateLambda([&](const TCHAR* Name, IConsoleObject* Obj)
		{
			if (Count >= MaxResults)
			{
				return;
			}

			IConsoleVariable* CVar = Obj->AsVariable();
			if (!CVar)
			{
				return;
			}

			FString NameStr(Name);
			if (!Filter.IsEmpty() && !NameStr.Contains(Filter))
			{
				return;
			}

			TSharedRef<FJsonObject> CVarObj = MakeShared<FJsonObject>();
			CVarObj->SetStringField(TEXT("name"), NameStr);
			CVarObj->SetStringField(TEXT("value"), CVar->GetString());
			CVarObj->SetStringField(TEXT("help"), CVar->GetHelp());

			CVarArray.Add(MakeShared<FJsonValueObject>(CVarObj));
			Count++;
		}),
		TEXT("")
	);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("count"), CVarArray.Num());
	OutJson->SetStringField(TEXT("filter"), Filter);
	OutJson->SetArrayField(TEXT("cvars"), CVarArray);

	return JsonToString(OutJson);
}
