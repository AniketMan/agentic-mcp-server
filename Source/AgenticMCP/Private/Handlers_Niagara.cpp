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
#include "NiagaraEmitterHandle.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPNiagara, Log, All);

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
	AActor* FoundActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundComp || !FoundComp->GetAsset())
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ParamsArray;

	// Get the Niagara system and read exposed parameters
	UNiagaraSystem* System = FoundComp->GetAsset();

	// Read user parameters from the component's override parameters
	const FNiagaraParameterStore& OverrideParams = FoundComp->GetOverrideParameters();
	TArray<FNiagaraVariableWithOffset> Parameters;
	OverrideParams.GetParameters(Parameters);

	for (const FNiagaraVariableWithOffset& ParamWithOffset : Parameters)
	{
		const FNiagaraVariable& Param = ParamWithOffset;
		TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();

		ParamObj->SetStringField(TEXT("name"), Param.GetName().ToString());
		ParamObj->SetStringField(TEXT("type"), Param.GetType().GetName());

		// Try to read value based on type
		if (Param.GetType() == FNiagaraTypeDefinition::GetFloatDef())
		{
			float Value = 0.0f;
			if (OverrideParams.GetParameterValue(Value, Param))
			{
				ParamObj->SetNumberField(TEXT("value"), Value);
			}
		}
		else if (Param.GetType() == FNiagaraTypeDefinition::GetIntDef())
		{
			int32 Value = 0;
			if (OverrideParams.GetParameterValue(Value, Param))
			{
				ParamObj->SetNumberField(TEXT("value"), Value);
			}
		}
		else if (Param.GetType() == FNiagaraTypeDefinition::GetBoolDef())
		{
			FNiagaraBool Value;
			if (OverrideParams.GetParameterValue(Value, Param))
			{
				ParamObj->SetBoolField(TEXT("value"), Value.GetValue());
			}
		}
		else if (Param.GetType() == FNiagaraTypeDefinition::GetVec3Def())
		{
			FVector Value = FVector::ZeroVector;
			if (OverrideParams.GetParameterValue(Value, Param))
			{
				TSharedRef<FJsonObject> VecObj = MakeShared<FJsonObject>();
				VecObj->SetNumberField(TEXT("x"), Value.X);
				VecObj->SetNumberField(TEXT("y"), Value.Y);
				VecObj->SetNumberField(TEXT("z"), Value.Z);
				ParamObj->SetObjectField(TEXT("value"), VecObj);
			}
		}
		else if (Param.GetType() == FNiagaraTypeDefinition::GetColorDef())
		{
			FLinearColor Value = FLinearColor::White;
			if (OverrideParams.GetParameterValue(Value, Param))
			{
				TSharedRef<FJsonObject> ColorObj = MakeShared<FJsonObject>();
				ColorObj->SetNumberField(TEXT("r"), Value.R);
				ColorObj->SetNumberField(TEXT("g"), Value.G);
				ColorObj->SetNumberField(TEXT("b"), Value.B);
				ColorObj->SetNumberField(TEXT("a"), Value.A);
				ParamObj->SetObjectField(TEXT("value"), ColorObj);
			}
		}

		ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	// Also list system-level user exposed parameters
	TArray<TSharedPtr<FJsonValue>> ExposedParamsArray;
	const TArray<FNiagaraVariable>& ExposedVars = System->GetExposedParameters().ReadParameterVariables();
	for (const FNiagaraVariable& Var : ExposedVars)
	{
		TSharedRef<FJsonObject> ExpObj = MakeShared<FJsonObject>();
		ExpObj->SetStringField(TEXT("name"), Var.GetName().ToString());
		ExpObj->SetStringField(TEXT("type"), Var.GetType().GetName());
		ExposedParamsArray.Add(MakeShared<FJsonValueObject>(ExpObj));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetName());
	Result->SetStringField(TEXT("systemName"), System->GetName());
	Result->SetArrayField(TEXT("overrideParameters"), ParamsArray);
	Result->SetNumberField(TEXT("overrideCount"), ParamsArray.Num());
	Result->SetArrayField(TEXT("exposedParameters"), ExposedParamsArray);
	Result->SetNumberField(TEXT("exposedCount"), ExposedParamsArray.Num());

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

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
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

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UNiagaraComponent* FoundComp = nullptr;
	AActor* FoundActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName)
		{
			FoundComp = Actor->FindComponentByClass<UNiagaraComponent>();
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundComp || !FoundComp->GetAsset())
	{
		return MakeErrorJson(FString::Printf(TEXT("Niagara component not found on actor: %s"), *ActorName));
	}

	// Find the emitter by name and set its enabled state
	UNiagaraSystem* System = FoundComp->GetAsset();
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();

	bool bFoundEmitter = false;
	int32 EmitterIndex = -1;

	// Try to find by exact name or index
	if (EmitterName.IsNumeric())
	{
		EmitterIndex = FCString::Atoi(*EmitterName);
		if (EmitterIndex >= 0 && EmitterIndex < EmitterHandles.Num())
		{
			bFoundEmitter = true;
		}
	}
	else
	{
		for (int32 i = 0; i < EmitterHandles.Num(); i++)
		{
			const FNiagaraEmitterHandle& Handle = EmitterHandles[i];
			if (Handle.GetName().ToString() == EmitterName ||
				Handle.GetName().ToString().Contains(EmitterName))
			{
				EmitterIndex = i;
				bFoundEmitter = true;
				break;
			}
		}
	}

	if (!bFoundEmitter || EmitterIndex < 0)
	{
		// List available emitters for debugging
		TArray<FString> AvailableEmitters;
		for (int32 i = 0; i < EmitterHandles.Num(); i++)
		{
			AvailableEmitters.Add(FString::Printf(TEXT("%d: %s"), i, *EmitterHandles[i].GetName().ToString()));
		}
		return MakeErrorJson(FString::Printf(TEXT("Emitter '%s' not found. Available emitters: %s"),
			*EmitterName, *FString::Join(AvailableEmitters, TEXT(", "))));
	}

	// Set emitter enabled state
	FoundComp->SetEmitterEnable(EmitterHandles[EmitterIndex].GetName(), bEnabled);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetName());
	Result->SetStringField(TEXT("emitterName"), EmitterHandles[EmitterIndex].GetName().ToString());
	Result->SetNumberField(TEXT("emitterIndex"), EmitterIndex);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetBoolField(TEXT("systemActive"), FoundComp->IsActive());

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

// ============================================================================
// NIAGARA MUTATION HANDLERS
// ============================================================================

// --- niagaraCreateSystem ---
FString FAgenticMCPServer::HandleNiagaraCreateSystem(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString AssetPath = Json->GetStringField(TEXT("path"));
	if (AssetPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'path'"));

	FString PackageName = AssetPath;
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
		return MakeErrorJson(TEXT("Failed to create package"));

	UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!System)
		return MakeErrorJson(TEXT("Failed to create Niagara system"));

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(System);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("path"), AssetPath);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- niagaraAddEmitter ---
FString FAgenticMCPServer::HandleNiagaraAddEmitter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SystemPath = Json->GetStringField(TEXT("systemPath"));
	FString EmitterPath = Json->GetStringField(TEXT("emitterPath"));
	if (SystemPath.IsEmpty() || EmitterPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'systemPath' or 'emitterPath'"));

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
		return MakeErrorJson(FString::Printf(TEXT("System not found: %s"), *SystemPath));

	UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
	if (!Emitter)
		return MakeErrorJson(FString::Printf(TEXT("Emitter not found: %s"), *EmitterPath));

	System->AddEmitterHandle(*Emitter, FName(*FPaths::GetBaseFilename(EmitterPath)));
	System->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("emitter"), EmitterPath);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- niagaraRemoveEmitter ---
FString FAgenticMCPServer::HandleNiagaraRemoveEmitter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SystemPath = Json->GetStringField(TEXT("systemPath"));
	int32 EmitterIndex = (int32)Json->GetNumberField(TEXT("emitterIndex"));

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
		return MakeErrorJson(FString::Printf(TEXT("System not found: %s"), *SystemPath));

	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	if (EmitterIndex < 0 || EmitterIndex >= Handles.Num())
		return MakeErrorJson(FString::Printf(TEXT("Emitter index %d out of range (0-%d)"), EmitterIndex, Handles.Num() - 1));

	System->RemoveEmitterHandleByIndex(EmitterIndex);
	System->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("removedIndex"), EmitterIndex);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- niagaraSetSystemProperty ---
FString FAgenticMCPServer::HandleNiagaraSetSystemProperty(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SystemPath = Json->GetStringField(TEXT("systemPath"));
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
		return MakeErrorJson(FString::Printf(TEXT("System not found: %s"), *SystemPath));

	int32 Changed = 0;
	bool BoolVal;
	double NumVal;

	if (Json->TryGetBoolField(TEXT("warmupEnabled"), BoolVal))
	{
		System->SetWarmupTime(BoolVal ? 1.0f : 0.0f);
		Changed++;
	}
	if (Json->TryGetNumberField(TEXT("warmupTime"), NumVal))
	{
		System->SetWarmupTime((float)NumVal);
		Changed++;
	}
	if (Json->TryGetNumberField(TEXT("warmupTickCount"), NumVal))
	{
		System->SetWarmupTickCount((int32)NumVal);
		Changed++;
	}
	if (Json->TryGetBoolField(TEXT("fixedBounds"), BoolVal))
	{
		System->bFixedBounds = BoolVal;
		Changed++;
	}

	System->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("fieldsChanged"), Changed);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- niagaraSpawnSystem ---
FString FAgenticMCPServer::HandleNiagaraSpawnSystem(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SystemPath = Json->GetStringField(TEXT("systemPath"));
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
		return MakeErrorJson(FString::Printf(TEXT("System not found: %s"), *SystemPath));

	FVector Location = FVector::ZeroVector;
	const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
	if (Json->TryGetArrayField(TEXT("location"), LocArray) && LocArray->Num() >= 3)
		Location = FVector((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber());

	FRotator Rotation = FRotator::ZeroRotator;
	const TArray<TSharedPtr<FJsonValue>>* RotArray = nullptr;
	if (Json->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
		Rotation = FRotator((*RotArray)[0]->AsNumber(), (*RotArray)[1]->AsNumber(), (*RotArray)[2]->AsNumber());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation);
	if (!NiagaraComp)
		return MakeErrorJson(TEXT("Failed to spawn Niagara system"));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("component"), NiagaraComp->GetName());
	Result->SetStringField(TEXT("owner"), NiagaraComp->GetOwner() ? NiagaraComp->GetOwner()->GetName() : TEXT("none"));
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}
