// Handlers_Story.cpp
// Story progression handlers for AgenticMCP
// Controls story state, advancement, and navigation via BP_StoryController

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"

FString FAgenticMCPServer::HandleStoryState(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No PIE world active. Start PIE first."));
	}

	// Find BP_StoryController in the world
	AActor* StoryController = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Contains(TEXT("StoryController")) ||
			It->GetClass()->GetName().Contains(TEXT("BP_StoryController")))
		{
			StoryController = *It;
			break;
		}
	}

	if (!StoryController)
	{
		return MakeErrorJson(TEXT("BP_StoryController not found in world"));
	}

	Result->SetStringField(TEXT("controllerName"), StoryController->GetName());
	Result->SetStringField(TEXT("controllerClass"), StoryController->GetClass()->GetName());

	// Get CurrentStoryIndex property
	FProperty* IndexProp = StoryController->GetClass()->FindPropertyByName(TEXT("CurrentStoryIndex"));
	if (IndexProp)
	{
		int32 CurrentIndex = 0;
		IndexProp->GetValue_InContainer(StoryController, &CurrentIndex);
		Result->SetNumberField(TEXT("currentStoryIndex"), CurrentIndex);
	}

	// Get CurrentSequence property if exists
	FProperty* SeqProp = StoryController->GetClass()->FindPropertyByName(TEXT("CurrentSequence"));
	if (SeqProp)
	{
		FString SeqName;
		if (FStrProperty* StrProp = CastField<FStrProperty>(SeqProp))
		{
			SeqName = StrProp->GetPropertyValue_InContainer(StoryController);
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(SeqProp))
		{
			UObject* SeqObj = ObjProp->GetObjectPropertyValue_InContainer(StoryController);
			if (SeqObj)
			{
				SeqName = SeqObj->GetName();
			}
		}
		if (!SeqName.IsEmpty())
		{
			Result->SetStringField(TEXT("currentSequence"), SeqName);
		}
	}

	// Get IsPlaying property if exists
	FProperty* PlayingProp = StoryController->GetClass()->FindPropertyByName(TEXT("bIsPlaying"));
	if (!PlayingProp)
	{
		PlayingProp = StoryController->GetClass()->FindPropertyByName(TEXT("IsPlaying"));
	}
	if (PlayingProp)
	{
		bool bIsPlaying = false;
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(PlayingProp))
		{
			bIsPlaying = BoolProp->GetPropertyValue_InContainer(StoryController);
		}
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
	}

	// Try to get total steps from DataTable reference
	FProperty* DataTableProp = StoryController->GetClass()->FindPropertyByName(TEXT("StoryStepsTable"));
	if (!DataTableProp)
	{
		DataTableProp = StoryController->GetClass()->FindPropertyByName(TEXT("DT_StorySteps"));
	}
	if (DataTableProp)
	{
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(DataTableProp))
		{
			UObject* DTObj = ObjProp->GetObjectPropertyValue_InContainer(StoryController);
			if (DTObj)
			{
				Result->SetStringField(TEXT("dataTable"), DTObj->GetName());
			}
		}
	}

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStoryAdvance(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No PIE world active. Start PIE first."));
	}

	// Find BP_StoryController
	AActor* StoryController = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Contains(TEXT("StoryController")) ||
			It->GetClass()->GetName().Contains(TEXT("BP_StoryController")))
		{
			StoryController = *It;
			break;
		}
	}

	if (!StoryController)
	{
		return MakeErrorJson(TEXT("BP_StoryController not found in world"));
	}

	// Get current index before advancing
	int32 PreviousIndex = -1;
	FProperty* IndexProp = StoryController->GetClass()->FindPropertyByName(TEXT("CurrentStoryIndex"));
	if (IndexProp)
	{
		IndexProp->GetValue_InContainer(StoryController, &PreviousIndex);
	}

	// Call AdvanceStory function on the controller
	UFunction* AdvanceFunc = StoryController->GetClass()->FindFunctionByName(TEXT("AdvanceStory"));
	if (!AdvanceFunc)
	{
		AdvanceFunc = StoryController->GetClass()->FindFunctionByName(TEXT("NextStep"));
	}
	if (!AdvanceFunc)
	{
		AdvanceFunc = StoryController->GetClass()->FindFunctionByName(TEXT("GoToNextStep"));
	}

	if (AdvanceFunc)
	{
		StoryController->ProcessEvent(AdvanceFunc, nullptr);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("method"), TEXT("function"));
		Result->SetStringField(TEXT("functionCalled"), AdvanceFunc->GetName());
	}
	else
	{
		// Fallback: manually increment CurrentStoryIndex
		if (IndexProp)
		{
			int32 NewIndex = PreviousIndex + 1;
			IndexProp->SetValue_InContainer(StoryController, &NewIndex);
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("method"), TEXT("property"));
		}
		else
		{
			return MakeErrorJson(TEXT("No AdvanceStory function or CurrentStoryIndex property found"));
		}
	}

	// Get new index after advancing
	int32 NewIndex = -1;
	if (IndexProp)
	{
		IndexProp->GetValue_InContainer(StoryController, &NewIndex);
	}

	Result->SetNumberField(TEXT("previousIndex"), PreviousIndex);
	Result->SetNumberField(TEXT("newIndex"), NewIndex);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStoryGoto(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"index\": N} or {\"stepName\": \"StoryStep_X\"}"));
	}

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No PIE world active. Start PIE first."));
	}

	// Find BP_StoryController
	AActor* StoryController = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Contains(TEXT("StoryController")) ||
			It->GetClass()->GetName().Contains(TEXT("BP_StoryController")))
		{
			StoryController = *It;
			break;
		}
	}

	if (!StoryController)
	{
		return MakeErrorJson(TEXT("BP_StoryController not found in world"));
	}

	int32 TargetIndex = -1;

	// Check for index parameter
	if (BodyJson->HasField(TEXT("index")))
	{
		TargetIndex = static_cast<int32>(BodyJson->GetNumberField(TEXT("index")));
	}
	else if (BodyJson->HasField(TEXT("stepName")))
	{
		// TODO: Look up step name in DataTable to get index
		FString StepName = BodyJson->GetStringField(TEXT("stepName"));
		// For now, extract number from step name like "StoryStep_5" -> 5
		FString NumStr;
		for (int32 i = StepName.Len() - 1; i >= 0; --i)
		{
			if (FChar::IsDigit(StepName[i]))
			{
				NumStr = FString::Chr(StepName[i]) + NumStr;
			}
			else if (!NumStr.IsEmpty())
			{
				break;
			}
		}
		if (!NumStr.IsEmpty())
		{
			TargetIndex = FCString::Atoi(*NumStr);
		}
	}

	if (TargetIndex < 0)
	{
		return MakeErrorJson(TEXT("Invalid target index. Provide 'index' (number) or 'stepName' (string)"));
	}

	// Get current index
	int32 PreviousIndex = -1;
	FProperty* IndexProp = StoryController->GetClass()->FindPropertyByName(TEXT("CurrentStoryIndex"));
	if (IndexProp)
	{
		IndexProp->GetValue_InContainer(StoryController, &PreviousIndex);
	}

	// Try to find a GoToStep function first
	UFunction* GotoFunc = StoryController->GetClass()->FindFunctionByName(TEXT("GoToStep"));
	if (!GotoFunc)
	{
		GotoFunc = StoryController->GetClass()->FindFunctionByName(TEXT("JumpToStep"));
	}
	if (!GotoFunc)
	{
		GotoFunc = StoryController->GetClass()->FindFunctionByName(TEXT("SetStoryIndex"));
	}

	if (GotoFunc)
	{
		// Call function with index parameter
		struct { int32 Index; } Parms;
		Parms.Index = TargetIndex;
		StoryController->ProcessEvent(GotoFunc, &Parms);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("method"), TEXT("function"));
		Result->SetStringField(TEXT("functionCalled"), GotoFunc->GetName());
	}
	else if (IndexProp)
	{
		// Fallback: directly set the property
		IndexProp->SetValue_InContainer(StoryController, &TargetIndex);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("method"), TEXT("property"));
	}
	else
	{
		return MakeErrorJson(TEXT("No GoToStep function or CurrentStoryIndex property found"));
	}

	// Verify the new index
	int32 NewIndex = -1;
	if (IndexProp)
	{
		IndexProp->GetValue_InContainer(StoryController, &NewIndex);
	}

	Result->SetNumberField(TEXT("previousIndex"), PreviousIndex);
	Result->SetNumberField(TEXT("targetIndex"), TargetIndex);
	Result->SetNumberField(TEXT("newIndex"), NewIndex);

	return JsonToString(Result);
}
