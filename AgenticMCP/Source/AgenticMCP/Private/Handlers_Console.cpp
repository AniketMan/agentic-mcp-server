// Handlers_Console.cpp
// Console command handlers for AgenticMCP.
// Provides: execute console command, get/set CVars, list CVars

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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), TEXT("Console command executed"));

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("value"), CVar->GetString());

	// Add type info
	if (CVar->IsVariableInt())
	{
		Result->SetStringField(TEXT("type"), TEXT("int"));
		Result->SetNumberField(TEXT("intValue"), CVar->GetInt());
	}
	else if (CVar->IsVariableFloat())
	{
		Result->SetStringField(TEXT("type"), TEXT("float"));
		Result->SetNumberField(TEXT("floatValue"), CVar->GetFloat());
	}
	else if (CVar->IsVariableBool())
	{
		Result->SetStringField(TEXT("type"), TEXT("bool"));
		Result->SetBoolField(TEXT("boolValue"), CVar->GetBool());
	}
	else
	{
		Result->SetStringField(TEXT("type"), TEXT("string"));
	}

	// Get help text
	Result->SetStringField(TEXT("help"), CVar->GetHelp());

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("oldValue"), OldValue);
	Result->SetStringField(TEXT("newValue"), CVar->GetString());

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleListCVars(const TMap<FString, FString>& Params, const FString& Body)
{
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), CVarArray.Num());
	Result->SetStringField(TEXT("filter"), Filter);
	Result->SetArrayField(TEXT("cvars"), CVarArray);

	return JsonToString(Result);
}
