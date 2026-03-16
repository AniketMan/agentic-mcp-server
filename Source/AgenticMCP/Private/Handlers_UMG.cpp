// Handlers_UMG.cpp
// UMG / Widget Blueprint inspection and editing handlers for AgenticMCP.
// Provides visibility into UI widgets, widget blueprints, and HUD components.
//
// Endpoints:
//   umgListWidgets - List all Widget Blueprint assets
//   umgGetWidgetTree - Get the widget hierarchy tree of a Widget Blueprint
//   umgGetWidgetProperties - Get properties of a specific widget in a Widget Blueprint
//   umgListHUDs - List all HUD actors in the level

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "GameFramework/HUD.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ============================================================
// umgListWidgets - List all Widget Blueprint assets
// ============================================================
FString FAgenticMCPServer::HandleUMGListWidgets(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> WidgetAssets;
	AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), WidgetAssets, true);

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
		Filter = BodyJson->GetStringField(TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> WidgetArr;
	for (const FAssetData& Asset : WidgetAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		WidgetArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), WidgetArr.Num());
	OutJson->SetArrayField(TEXT("widgetBlueprints"), WidgetArr);
	return JsonToString(OutJson);
}

// ============================================================
// Helper: Recursively serialize widget tree
// ============================================================
static TSharedRef<FJsonObject> SerializeWidget(UWidget* Widget, int32 Depth = 0)
{
	TSharedRef<FJsonObject> WidgetJson = MakeShared<FJsonObject>();
	if (!Widget) return WidgetJson;

	WidgetJson->SetStringField(TEXT("name"), Widget->GetName());
	WidgetJson->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
	WidgetJson->SetNumberField(TEXT("depth"), Depth);
	WidgetJson->SetBoolField(TEXT("isVisible"), Widget->GetVisibility() == ESlateVisibility::Visible ||
		Widget->GetVisibility() == ESlateVisibility::HitTestInvisible ||
		Widget->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);

	// Type-specific info
	if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
	{
		WidgetJson->SetStringField(TEXT("text"), TextBlock->GetText().ToString());
	}

	// Children
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			if (Child)
			{
				ChildArr.Add(MakeShared<FJsonValueObject>(SerializeWidget(Child, Depth + 1)));
			}
		}
		WidgetJson->SetArrayField(TEXT("children"), ChildArr);
	}

	return WidgetJson;
}

// ============================================================
// umgGetWidgetTree - Get widget hierarchy tree
// ============================================================
FString FAgenticMCPServer::HandleUMGGetWidgetTree(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> WidgetAssets;
	AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), WidgetAssets, true);

	UWidgetBlueprint* WBP = nullptr;
	for (const FAssetData& Asset : WidgetAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			WBP = Cast<UWidgetBlueprint>(Asset.GetAsset());
			break;
		}
	}
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget Blueprint not found: %s"), *Name));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("Widget Blueprint has no widget tree"));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), WBP->GetName());
	OutJson->SetStringField(TEXT("path"), WBP->GetPathName());

	UWidget* RootWidget = Tree->RootWidget;
	if (RootWidget)
	{
		OutJson->SetObjectField(TEXT("rootWidget"), SerializeWidget(RootWidget));
	}

	// All widgets flat list
	TArray<UWidget*> AllWidgets;
	Tree->GetAllWidgets(AllWidgets);
	OutJson->SetNumberField(TEXT("totalWidgetCount"), AllWidgets.Num());

	return JsonToString(OutJson);
}

// ============================================================
// umgGetWidgetProperties - Get properties of a specific widget
// ============================================================
FString FAgenticMCPServer::HandleUMGGetWidgetProperties(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprintName"));
	FString WidgetName = Json->GetStringField(TEXT("widgetName"));
	if (BlueprintName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: blueprintName"));
	if (WidgetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetName"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> WidgetAssets;
	AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), WidgetAssets, true);

	UWidgetBlueprint* WBP = nullptr;
	for (const FAssetData& Asset : WidgetAssets)
	{
		if (Asset.AssetName.ToString() == BlueprintName || Asset.GetObjectPathString().Contains(BlueprintName))
		{
			WBP = Cast<UWidgetBlueprint>(Asset.GetAsset());
			break;
		}
	}
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget Blueprint not found: %s"), *BlueprintName));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("Widget Blueprint has no widget tree"));

	// Find the widget by name
	UWidget* FoundWidget = nullptr;
	TArray<UWidget*> AllWidgets;
	Tree->GetAllWidgets(AllWidgets);
	for (UWidget* W : AllWidgets)
	{
		if (W && (W->GetName() == WidgetName))
		{
			FoundWidget = W;
			break;
		}
	}
	if (!FoundWidget) return MakeErrorJson(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), FoundWidget->GetName());
	OutJson->SetStringField(TEXT("class"), FoundWidget->GetClass()->GetName());
	OutJson->SetStringField(TEXT("visibility"), StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)FoundWidget->GetVisibility()));
	OutJson->SetBoolField(TEXT("isEnabled"), FoundWidget->GetIsEnabled());

	// Render transform
	FWidgetTransform RenderTransform = FoundWidget->GetRenderTransform();
	OutJson->SetNumberField(TEXT("renderTranslationX"), RenderTransform.Translation.X);
	OutJson->SetNumberField(TEXT("renderTranslationY"), RenderTransform.Translation.Y);
	OutJson->SetNumberField(TEXT("renderAngle"), RenderTransform.Angle);
	OutJson->SetNumberField(TEXT("renderScaleX"), RenderTransform.Scale.X);
	OutJson->SetNumberField(TEXT("renderScaleY"), RenderTransform.Scale.Y);

	// Type-specific properties
	if (UTextBlock* TB = Cast<UTextBlock>(FoundWidget))
	{
		OutJson->SetStringField(TEXT("text"), TB->GetText().ToString());
		OutJson->SetNumberField(TEXT("fontSize"), TB->GetFont().Size);
	}
	else if (UProgressBar* PB = Cast<UProgressBar>(FoundWidget))
	{
		OutJson->SetNumberField(TEXT("percent"), PB->GetPercent());
	}
	else if (UImage* Img = Cast<UImage>(FoundWidget))
	{
		OutJson->SetStringField(TEXT("widgetType"), TEXT("Image"));
	}
	else if (UButton* Btn = Cast<UButton>(FoundWidget))
	{
		OutJson->SetBoolField(TEXT("isInteractionEnabled"), Btn->GetIsEnabled());
	}

	return JsonToString(OutJson);
}

// ============================================================
// umgListHUDs - List all HUD actors in the level
// ============================================================
FString FAgenticMCPServer::HandleUMGListHUDs(const FString& Body)
{
	UWorld* World = nullptr;
	if (GEditor) World = GEditor->GetEditorWorldContext().World();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> HUDArr;
	for (TActorIterator<AHUD> It(World); It; ++It)
	{
		AHUD* HUD = *It;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), HUD->GetActorLabel());
		Entry->SetStringField(TEXT("class"), HUD->GetClass()->GetName());
		HUDArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), HUDArr.Num());
	OutJson->SetArrayField(TEXT("huds"), HUDArr);
	return JsonToString(OutJson);
}
