// Handlers_Niagara.cpp
// Niagara particle system debugging and control endpoints for AgenticMCP
// Provides visibility into particle systems, emitters, parameters, and performance stats

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraParameterStore.h"

// ============================================================
// HandleNiagaraGetStatus
// GET /api/niagara/status
// Returns overall Niagara system status
// ============================================================
FString FAgenticMCPServer::HandleNiagaraGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> StatusObj = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	int32 ActiveSystems = 0;
	int32 TotalComponents = 0;
	int32 ActiveComponents = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TArray<UNiagaraComponent*> NiagaraComps;
		Actor->GetComponents<UNiagaraComponent>(NiagaraComps);

		for (UNiagaraComponent* Comp : NiagaraComps)
		{
			TotalComponents++;
			if (Comp->IsActive())
			{
				ActiveComponents++;
				if (Comp->GetAsset())
				{
					ActiveSystems++;
				}
			}
		}
	}

	StatusObj->SetNumberField(TEXT("totalNiagaraComponents"), TotalComponents);
	StatusObj->SetNumberField(TEXT("activeComponents"), ActiveComponents);
	StatusObj->SetNumberField(TEXT("activeSystems"), ActiveSystems);
	StatusObj->SetBoolField(TEXT("niagaraModuleLoaded"), true);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("status"), StatusObj);

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraListSystems
// GET /api/niagara/systems
// Lists all Niagara systems in the current level
// ============================================================
FString FAgenticMCPServer::HandleNiagaraListSystems(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SystemsArray;

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TArray<UNiagaraComponent*> NiagaraComps;
		Actor->GetComponents<UNiagaraComponent>(NiagaraComps);

		for (UNiagaraComponent* Comp : NiagaraComps)
		{
			if (Comp && Comp->GetAsset())
			{
				TSharedRef<FJsonObject> SystemObj = MakeShared<FJsonObject>();

				SystemObj->SetStringField(TEXT("componentName"), Comp->GetName());
				SystemObj->SetStringField(TEXT("actorName"), Actor->GetName());
				SystemObj->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				SystemObj->SetStringField(TEXT("systemAsset"), Comp->GetAsset()->GetPathName());
				SystemObj->SetStringField(TEXT("systemName"), Comp->GetAsset()->GetName());
				SystemObj->SetBoolField(TEXT("isActive"), Comp->IsActive());
				SystemObj->SetBoolField(TEXT("isComplete"), Comp->IsComplete());

				FVector Location = Comp->GetComponentLocation();
				TSharedRef<FJsonObject> LocObj = MakeShared<FJsonObject>();
				LocObj->SetNumberField(TEXT("x"), Location.X);
				LocObj->SetNumberField(TEXT("y"), Location.Y);
				LocObj->SetNumberField(TEXT("z"), Location.Z);
				SystemObj->SetObjectField(TEXT("location"), LocObj);

				SystemsArray.Add(MakeShared<FJsonValueObject>(SystemObj));
			}
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("systems"), SystemsArray);
	Result->SetNumberField(TEXT("count"), SystemsArray.Num());

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraGetSystemInfo
// POST /api/niagara/system-info
// ============================================================
FString FAgenticMCPServer::HandleNiagaraGetSystemInfo(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;
	AActor* FoundActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> InfoObj = MakeShared<FJsonObject>();

	InfoObj->SetStringField(TEXT("componentName"), FoundComp->GetName());
	InfoObj->SetStringField(TEXT("actorName"), FoundActor->GetName());

	if (UNiagaraSystem* System = FoundComp->GetAsset())
	{
		InfoObj->SetStringField(TEXT("systemAsset"), System->GetPathName());
		InfoObj->SetStringField(TEXT("systemName"), System->GetName());
	}

	InfoObj->SetBoolField(TEXT("isActive"), FoundComp->IsActive());
	InfoObj->SetBoolField(TEXT("isComplete"), FoundComp->IsComplete());
	InfoObj->SetBoolField(TEXT("isPaused"), FoundComp->IsPaused());

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("systemInfo"), InfoObj);

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraGetEmitters
// POST /api/niagara/emitters
// ============================================================
FString FAgenticMCPServer::HandleNiagaraGetEmitters(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			break;
		}
	}

	if (!FoundComp || !FoundComp->GetAsset())
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara system not found on actor: %s"), *ActorName));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> EmittersArray;

	UNiagaraSystem* System = FoundComp->GetAsset();
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();

	for (int32 i = 0; i < EmitterHandles.Num(); i++)
	{
		const FNiagaraEmitterHandle& Handle = EmitterHandles[i];
		TSharedRef<FJsonObject> EmitterObj = MakeShared<FJsonObject>();

		EmitterObj->SetNumberField(TEXT("index"), i);
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("isEnabled"), Handle.GetIsEnabled());

		EmittersArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetArrayField(TEXT("emitters"), EmittersArray);
	Result->SetNumberField(TEXT("count"), EmittersArray.Num());

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraSetParameter
// POST /api/niagara/set-parameter
// ============================================================
FString FAgenticMCPServer::HandleNiagaraSetParameter(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	FString ParameterName;
	if (!JsonBody->TryGetStringField(TEXT("parameterName"), ParameterName))
	{
		return MakeErrorJson(TEXT("Missing 'parameterName' field"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			break;
		}
	}

	if (!FoundComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	double FloatValue;
	if (JsonBody->TryGetNumberField(TEXT("value"), FloatValue))
	{
		FoundComp->SetVariableFloat(FName(*ParameterName), static_cast<float>(FloatValue));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetStringField(TEXT("parameterName"), ParameterName);
	Result->SetStringField(TEXT("message"), TEXT("Parameter set successfully"));

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraGetParameters
// POST /api/niagara/parameters
// ============================================================
FString FAgenticMCPServer::HandleNiagaraGetParameters(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			break;
		}
	}

	if (!FoundComp || !FoundComp->GetAsset())
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ParamsArray;

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetArrayField(TEXT("parameters"), ParamsArray);
	Result->SetNumberField(TEXT("count"), ParamsArray.Num());

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraActivateSystem
// POST /api/niagara/activate
// ============================================================
FString FAgenticMCPServer::HandleNiagaraActivateSystem(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	bool bActivate = true;
	JsonBody->TryGetBoolField(TEXT("activate"), bActivate);

	bool bResetSystem = false;
	JsonBody->TryGetBoolField(TEXT("resetSystem"), bResetSystem);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			break;
		}
	}

	if (!FoundComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	if (bActivate)
	{
		FoundComp->Activate(bResetSystem);
	}
	else
	{
		FoundComp->Deactivate();
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetBoolField(TEXT("activated"), bActivate);
	Result->SetBoolField(TEXT("isActive"), FoundComp->IsActive());

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraSetEmitterEnable
// POST /api/niagara/set-emitter
// ============================================================
FString FAgenticMCPServer::HandleNiagaraSetEmitterEnable(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	FString EmitterName;
	if (!JsonBody->TryGetStringField(TEXT("emitterName"), EmitterName))
	{
		return MakeErrorJson(TEXT("Missing 'emitterName' field"));
	}

	bool bEnabled = true;
	JsonBody->TryGetBoolField(TEXT("enabled"), bEnabled);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetStringField(TEXT("emitterName"), EmitterName);
	Result->SetBoolField(TEXT("enabled"), bEnabled);

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraResetSystem
// POST /api/niagara/reset
// ============================================================
FString FAgenticMCPServer::HandleNiagaraResetSystem(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	FString ActorName;
	if (!JsonBody->TryGetStringField(TEXT("actorName"), ActorName))
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			break;
		}
	}

	if (!FoundComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	FoundComp->ResetSystem();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetStringField(TEXT("message"), TEXT("System reset successfully"));

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraGetStats
// GET /api/niagara/stats
// ============================================================
FString FAgenticMCPServer::HandleNiagaraGetStats(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> StatsObj = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	int32 TotalComponents = 0;
	int32 ActiveComponents = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TArray<UNiagaraComponent*> NiagaraComps;
		Actor->GetComponents<UNiagaraComponent>(NiagaraComps);

		for (UNiagaraComponent* Comp : NiagaraComps)
		{
			TotalComponents++;
			if (Comp->IsActive())
			{
				ActiveComponents++;
			}
		}
	}

	StatsObj->SetNumberField(TEXT("totalComponents"), TotalComponents);
	StatsObj->SetNumberField(TEXT("activeComponents"), ActiveComponents);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("stats"), StatsObj);

	return JsonToString(Result);
}

// ============================================================
// HandleNiagaraDebugHUD
// POST /api/niagara/debug-hud
// ============================================================
FString FAgenticMCPServer::HandleNiagaraDebugHUD(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	bool bEnable = true;
	JsonBody->TryGetBoolField(TEXT("enable"), bEnable);

	FString Mode = TEXT("overview");
	JsonBody->TryGetStringField(TEXT("mode"), Mode);

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();

		if (bEnable)
		{
			GEngine->Exec(World, TEXT("fx.Niagara.Debug.Enabled 1"));
		}
		else
		{
			GEngine->Exec(World, TEXT("fx.Niagara.Debug.Enabled 0"));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("enabled"), bEnable);
	Result->SetStringField(TEXT("mode"), Mode);

	return JsonToString(Result);
}
