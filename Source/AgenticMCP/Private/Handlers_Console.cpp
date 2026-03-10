// Handlers_Console.cpp
// Console command handlers for AgenticMCP.
// Provides: execute console command, get/set CVars, list CVars

#include "AgenticMCPServer.h"
#include "Engine/Engine.h"
#include "Engine/Console.h"
#include "HAL/IConsoleManager.h"

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

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), TEXT("Console command executed"));

	return JsonToString(Result.ToSharedRef());
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

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
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

	return JsonToString(Result.ToSharedRef());
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

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("oldValue"), OldValue);
	Result->SetStringField(TEXT("newValue"), CVar->GetString());

	return JsonToString(Result.ToSharedRef());
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

			TSharedPtr<FJsonObject> CVarObj = MakeShared<FJsonObject>();
			CVarObj->SetStringField(TEXT("name"), NameStr);
			CVarObj->SetStringField(TEXT("value"), CVar->GetString());
			CVarObj->SetStringField(TEXT("help"), CVar->GetHelp());

			CVarArray.Add(MakeShared<FJsonValueObject>(CVarObj));
			Count++;
		}),
		TEXT("")
	);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), CVarArray.Num());
	Result->SetStringField(TEXT("filter"), Filter);
	Result->SetArrayField(TEXT("cvars"), CVarArray);

	return JsonToString(Result.ToSharedRef());
}
// Handlers_Console.cpp
// Console command and CVar handlers for AgenticMCP.
// Provides: execute console commands, get/set console variables

#include "AgenticMCPServer.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"

FString FAgenticMCPServer::HandleExecuteConsole(const TMap<FString, FString>& Params, const FString& Body)
{
	FString Command = Params.FindRef(TEXT("command"));
	if (Command.IsEmpty())
	{
		// Try to get from body
		TSharedPtr<FJsonObject> JsonBody = ParseJsonBody(Body);
		if (JsonBody.IsValid())
		{
			Command = JsonBody->GetStringField(TEXT("command"));
		}
	}

	if (Command.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'command' parameter"));
	}

	// Get target world
	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}
	else if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No world available"));
	}

	// Capture output
	FString OutputString;
	GLog->SetCurrentThreadAsMasterThread();

	// Execute the command
	GEngine->Exec(World, *Command);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), TEXT("Command executed"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleGetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	FString VarName = Params.FindRef(TEXT("name"));
	if (VarName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' parameter"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*VarName);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("Console variable '%s' not found"), *VarName));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), VarName);

	// Get the value based on type
	if (CVar->IsVariableBool())
	{
		Result->SetBoolField(TEXT("value"), CVar->GetBool());
		Result->SetStringField(TEXT("type"), TEXT("bool"));
	}
	else if (CVar->IsVariableInt())
	{
		Result->SetNumberField(TEXT("value"), CVar->GetInt());
		Result->SetStringField(TEXT("type"), TEXT("int"));
	}
	else if (CVar->IsVariableFloat())
	{
		Result->SetNumberField(TEXT("value"), CVar->GetFloat());
		Result->SetStringField(TEXT("type"), TEXT("float"));
	}
	else
	{
		Result->SetStringField(TEXT("value"), CVar->GetString());
		Result->SetStringField(TEXT("type"), TEXT("string"));
	}

	// Get help text
	Result->SetStringField(TEXT("help"), CVar->GetHelp());

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleSetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	FString VarName = Params.FindRef(TEXT("name"));
	FString Value = Params.FindRef(TEXT("value"));

	if (VarName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' parameter"));
	}

	if (Value.IsEmpty())
	{
		// Try body
		TSharedPtr<FJsonObject> JsonBody = ParseJsonBody(Body);
		if (JsonBody.IsValid())
		{
			VarName = JsonBody->GetStringField(TEXT("name"));

			// Handle different value types
			if (JsonBody->HasField(TEXT("value")))
			{
				TSharedPtr<FJsonValue> JsonValue = JsonBody->TryGetField(TEXT("value"));
				if (JsonValue->Type == EJson::Boolean)
				{
					Value = JsonValue->AsBool() ? TEXT("1") : TEXT("0");
				}
				else if (JsonValue->Type == EJson::Number)
				{
					Value = FString::SanitizeFloat(JsonValue->AsNumber());
				}
				else
				{
					Value = JsonValue->AsString();
				}
			}
		}
	}

	if (Value.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'value' parameter"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*VarName);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("Console variable '%s' not found"), *VarName));
	}

	// Check if read-only
	if (CVar->TestFlags(ECVF_ReadOnly))
	{
		return MakeErrorJson(FString::Printf(TEXT("Console variable '%s' is read-only"), *VarName));
	}

	// Set the value
	CVar->Set(*Value, ECVF_SetByConsole);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), VarName);
	Result->SetStringField(TEXT("newValue"), CVar->GetString());
	Result->SetStringField(TEXT("message"), TEXT("Console variable updated"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleListCVars(const TMap<FString, FString>& Params, const FString& Body)
{
	FString Filter = Params.FindRef(TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> CVarArray;

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
		FConsoleObjectVisitor::CreateLambda([&](const TCHAR* Name, IConsoleObject* Obj)
		{
			if (IConsoleVariable* CVar = Obj->AsVariable())
			{
				FString VarName(Name);

				// Apply filter if specified
				if (!Filter.IsEmpty() && !VarName.Contains(Filter))
				{
					return;
				}

				TSharedPtr<FJsonObject> CVarObj = MakeShared<FJsonObject>();
				CVarObj->SetStringField(TEXT("name"), VarName);
				CVarObj->SetStringField(TEXT("value"), CVar->GetString());
				CVarObj->SetStringField(TEXT("help"), CVar->GetHelp());

				// Determine type
				FString Type = TEXT("string");
				if (CVar->IsVariableBool()) Type = TEXT("bool");
				else if (CVar->IsVariableInt()) Type = TEXT("int");
				else if (CVar->IsVariableFloat()) Type = TEXT("float");
				CVarObj->SetStringField(TEXT("type"), Type);

				CVarObj->SetBoolField(TEXT("readOnly"), CVar->TestFlags(ECVF_ReadOnly));

				CVarArray.Add(MakeShared<FJsonValueObject>(CVarObj));
			}
		}),
		TEXT("")
	);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), CVarArray.Num());
	Result->SetArrayField(TEXT("cvars"), CVarArray);

	return JsonToString(Result);
}
