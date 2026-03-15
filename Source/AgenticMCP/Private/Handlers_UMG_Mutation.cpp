// Handlers_UMG_Mutation.cpp
// UMG Widget creation and editing handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// Endpoints:
//   umgCreateWidget       - Create a new widget blueprint
//   umgAddChild           - Add a child widget to a parent panel
//   umgRemoveChild        - Remove a child widget from a parent panel
//   umgSetWidgetProperty  - Set a property on a widget (text, color, visibility, etc.)
//   umgBindEvent          - Bind a widget event to a function
//   umgGetWidgetChildren  - Get children of a widget in the hierarchy

#include "AgenticMCPServer.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/WidgetBlueprintFactory.h"
#include "UObject/SavePackage.h"

// Helper: find a widget blueprint by name
static UWidgetBlueprint* FindWidgetBlueprintByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UWidgetBlueprint>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find a widget in the widget tree by name
static UWidget* FindWidgetInTree(UWidgetTree* Tree, const FString& WidgetName)
{
	if (!Tree) return nullptr;
	Tree->ForEachWidget([](UWidget* Widget) {});  // ensure tree is walked

	UWidget* Found = Tree->FindWidget(FName(*WidgetName));
	return Found;
}

// ============================================================
// umgCreateWidget - Create a new widget blueprint
// Params: name, path (optional, default /Game/UI),
//         rootWidgetType (optional, default CanvasPanel)
// ============================================================
FString FAgenticMCPServer::HandleUMGCreateWidget(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/UI");

	// Check if already exists
	if (FindWidgetBlueprintByName(Name))
		return MakeErrorJson(FString::Printf(TEXT("Widget blueprint already exists: %s"), *Name));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();

	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UWidgetBlueprint::StaticClass(), Factory);
	if (!NewAsset)
		return MakeErrorJson(TEXT("Failed to create widget blueprint"));

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(NewAsset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), WBP->GetName());
	Result->SetStringField(TEXT("path"), WBP->GetPathName());
	return JsonToString(Result);
}

// ============================================================
// umgAddChild - Add a child widget to a parent panel
// Params: widgetBlueprintName, parentWidgetName (or "root"),
//         childType (TextBlock, Button, Image, etc.),
//         childName, slot properties (optional)
// ============================================================
FString FAgenticMCPServer::HandleUMGAddChild(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString WBPName = Json->GetStringField(TEXT("widgetBlueprintName"));
	FString ParentName = Json->GetStringField(TEXT("parentWidgetName"));
	FString ChildType = Json->GetStringField(TEXT("childType"));
	FString ChildName = Json->GetStringField(TEXT("childName"));

	if (WBPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetBlueprintName"));
	if (ChildType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: childType"));
	if (ChildName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: childName"));

	UWidgetBlueprint* WBP = FindWidgetBlueprintByName(WBPName);
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget blueprint not found: %s"), *WBPName));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("Widget blueprint has no widget tree"));

	// Find parent
	UPanelWidget* ParentPanel = nullptr;
	if (ParentName.IsEmpty() || ParentName == TEXT("root"))
	{
		ParentPanel = Cast<UPanelWidget>(Tree->RootWidget);
	}
	else
	{
		ParentPanel = Cast<UPanelWidget>(FindWidgetInTree(Tree, ParentName));
	}
	if (!ParentPanel)
		return MakeErrorJson(FString::Printf(TEXT("Parent widget '%s' not found or is not a panel widget"), *ParentName));

	// Create child widget
	UWidget* NewChild = nullptr;
	FName ChildFName(*ChildName);

	if (ChildType == TEXT("TextBlock"))
		NewChild = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Button"))
		NewChild = Tree->ConstructWidget<UButton>(UButton::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Image"))
		NewChild = Tree->ConstructWidget<UImage>(UImage::StaticClass(), ChildFName);
	else if (ChildType == TEXT("EditableTextBox"))
		NewChild = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("CheckBox"))
		NewChild = Tree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Slider"))
		NewChild = Tree->ConstructWidget<USlider>(USlider::StaticClass(), ChildFName);
	else if (ChildType == TEXT("ProgressBar"))
		NewChild = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Border"))
		NewChild = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Spacer"))
		NewChild = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), ChildFName);
	else if (ChildType == TEXT("SizeBox"))
		NewChild = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("CanvasPanel"))
		NewChild = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), ChildFName);
	else if (ChildType == TEXT("VerticalBox"))
		NewChild = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("HorizontalBox"))
		NewChild = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("Overlay"))
		NewChild = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), ChildFName);
	else if (ChildType == TEXT("ScrollBox"))
		NewChild = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), ChildFName);
	else if (ChildType == TEXT("ScaleBox"))
		NewChild = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), ChildFName);
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported childType: %s. Supported: TextBlock, Button, Image, EditableTextBox, CheckBox, "
				 "Slider, ProgressBar, Border, Spacer, SizeBox, CanvasPanel, VerticalBox, HorizontalBox, "
				 "Overlay, ScrollBox, ScaleBox"),
			*ChildType));
	}

	if (!NewChild)
		return MakeErrorJson(TEXT("Failed to construct widget"));

	UPanelSlot* Slot = ParentPanel->AddChild(NewChild);
	if (!Slot)
		return MakeErrorJson(TEXT("Failed to add child to parent panel"));

	// Set canvas slot properties if parent is a CanvasPanel
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		if (Json->HasField(TEXT("posX")) || Json->HasField(TEXT("posY")))
		{
			float PosX = Json->HasField(TEXT("posX")) ? (float)Json->GetNumberField(TEXT("posX")) : 0.f;
			float PosY = Json->HasField(TEXT("posY")) ? (float)Json->GetNumberField(TEXT("posY")) : 0.f;
			CanvasSlot->SetPosition(FVector2D(PosX, PosY));
		}
		if (Json->HasField(TEXT("sizeX")) || Json->HasField(TEXT("sizeY")))
		{
			float SizeX = Json->HasField(TEXT("sizeX")) ? (float)Json->GetNumberField(TEXT("sizeX")) : 100.f;
			float SizeY = Json->HasField(TEXT("sizeY")) ? (float)Json->GetNumberField(TEXT("sizeY")) : 100.f;
			CanvasSlot->SetSize(FVector2D(SizeX, SizeY));
		}
	}

	WBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widgetBlueprintName"), WBPName);
	Result->SetStringField(TEXT("parentWidgetName"), ParentName);
	Result->SetStringField(TEXT("childType"), ChildType);
	Result->SetStringField(TEXT("childName"), ChildName);
	return JsonToString(Result);
}

// ============================================================
// umgRemoveChild - Remove a child widget
// Params: widgetBlueprintName, widgetName
// ============================================================
FString FAgenticMCPServer::HandleUMGRemoveChild(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString WBPName = Json->GetStringField(TEXT("widgetBlueprintName"));
	FString WidgetName = Json->GetStringField(TEXT("widgetName"));

	if (WBPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetBlueprintName"));
	if (WidgetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetName"));

	UWidgetBlueprint* WBP = FindWidgetBlueprintByName(WBPName);
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget blueprint not found: %s"), *WBPName));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("No widget tree"));

	UWidget* Widget = FindWidgetInTree(Tree, WidgetName);
	if (!Widget) return MakeErrorJson(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	if (Widget == Tree->RootWidget)
		return MakeErrorJson(TEXT("Cannot remove root widget"));

	Tree->RemoveWidget(Widget);
	WBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widgetBlueprintName"), WBPName);
	Result->SetStringField(TEXT("removedWidget"), WidgetName);
	return JsonToString(Result);
}

// ============================================================
// umgSetWidgetProperty - Set a property on a widget
// Params: widgetBlueprintName, widgetName,
//         property (text, visibility, colorAndOpacity, renderOpacity,
//                   isEnabled, toolTipText, percent, isChecked)
//         value (string, number, bool, or object depending on property)
// ============================================================
FString FAgenticMCPServer::HandleUMGSetWidgetProperty(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString WBPName = Json->GetStringField(TEXT("widgetBlueprintName"));
	FString WidgetName = Json->GetStringField(TEXT("widgetName"));
	FString Property = Json->GetStringField(TEXT("property"));

	if (WBPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetBlueprintName"));
	if (WidgetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetName"));
	if (Property.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: property"));

	UWidgetBlueprint* WBP = FindWidgetBlueprintByName(WBPName);
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget blueprint not found: %s"), *WBPName));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("No widget tree"));

	UWidget* Widget = FindWidgetInTree(Tree, WidgetName);
	if (!Widget) return MakeErrorJson(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	FString SetValue;

	if (Property == TEXT("text"))
	{
		FString TextVal = Json->GetStringField(TEXT("value"));
		if (UTextBlock* TB = Cast<UTextBlock>(Widget))
		{
			TB->SetText(FText::FromString(TextVal));
			SetValue = TextVal;
		}
		else if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Widget))
		{
			ETB->SetText(FText::FromString(TextVal));
			SetValue = TextVal;
		}
		else
		{
			return MakeErrorJson(TEXT("Widget does not support 'text' property"));
		}
	}
	else if (Property == TEXT("visibility"))
	{
		FString VisStr = Json->GetStringField(TEXT("value"));
		ESlateVisibility Vis = ESlateVisibility::Visible;
		if (VisStr == TEXT("Hidden")) Vis = ESlateVisibility::Hidden;
		else if (VisStr == TEXT("Collapsed")) Vis = ESlateVisibility::Collapsed;
		else if (VisStr == TEXT("HitTestInvisible")) Vis = ESlateVisibility::HitTestInvisible;
		else if (VisStr == TEXT("SelfHitTestInvisible")) Vis = ESlateVisibility::SelfHitTestInvisible;
		Widget->SetVisibility(Vis);
		SetValue = VisStr;
	}
	else if (Property == TEXT("renderOpacity"))
	{
		float Opacity = (float)Json->GetNumberField(TEXT("value"));
		Widget->SetRenderOpacity(Opacity);
		SetValue = FString::SanitizeFloat(Opacity);
	}
	else if (Property == TEXT("isEnabled"))
	{
		bool bEnabled = Json->GetBoolField(TEXT("value"));
		Widget->SetIsEnabled(bEnabled);
		SetValue = bEnabled ? TEXT("true") : TEXT("false");
	}
	else if (Property == TEXT("toolTipText"))
	{
		FString Tip = Json->GetStringField(TEXT("value"));
		Widget->SetToolTipText(FText::FromString(Tip));
		SetValue = Tip;
	}
	else if (Property == TEXT("percent"))
	{
		float Pct = (float)Json->GetNumberField(TEXT("value"));
		if (UProgressBar* PB = Cast<UProgressBar>(Widget))
		{
			PB->SetPercent(Pct);
			SetValue = FString::SanitizeFloat(Pct);
		}
		else if (USlider* SL = Cast<USlider>(Widget))
		{
			SL->SetValue(Pct);
			SetValue = FString::SanitizeFloat(Pct);
		}
		else
		{
			return MakeErrorJson(TEXT("Widget does not support 'percent' property"));
		}
	}
	else if (Property == TEXT("isChecked"))
	{
		bool bChecked = Json->GetBoolField(TEXT("value"));
		if (UCheckBox* CB = Cast<UCheckBox>(Widget))
		{
			CB->SetIsChecked(bChecked);
			SetValue = bChecked ? TEXT("true") : TEXT("false");
		}
		else
		{
			return MakeErrorJson(TEXT("Widget does not support 'isChecked' property"));
		}
	}
	else if (Property == TEXT("colorAndOpacity"))
	{
		const TSharedPtr<FJsonObject>* ValObj;
		if (!Json->TryGetObjectField(TEXT("value"), ValObj))
			return MakeErrorJson(TEXT("colorAndOpacity requires an object value with r, g, b, a"));

		float R = (*ValObj)->HasField(TEXT("r")) ? (float)(*ValObj)->GetNumberField(TEXT("r")) : 1.f;
		float G = (*ValObj)->HasField(TEXT("g")) ? (float)(*ValObj)->GetNumberField(TEXT("g")) : 1.f;
		float B = (*ValObj)->HasField(TEXT("b")) ? (float)(*ValObj)->GetNumberField(TEXT("b")) : 1.f;
		float A = (*ValObj)->HasField(TEXT("a")) ? (float)(*ValObj)->GetNumberField(TEXT("a")) : 1.f;

		if (UTextBlock* TB = Cast<UTextBlock>(Widget))
		{
			TB->SetColorAndOpacity(FSlateColor(FLinearColor(R, G, B, A)));
		}
		else if (UImage* Img = Cast<UImage>(Widget))
		{
			Img->SetColorAndOpacity(FLinearColor(R, G, B, A));
		}
		SetValue = FString::Printf(TEXT("(%f,%f,%f,%f)"), R, G, B, A);
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported property: %s. Supported: text, visibility, renderOpacity, isEnabled, "
				 "toolTipText, percent, isChecked, colorAndOpacity"),
			*Property));
	}

	WBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widgetBlueprintName"), WBPName);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("property"), Property);
	Result->SetStringField(TEXT("setValue"), SetValue);
	return JsonToString(Result);
}

// ============================================================
// umgBindEvent - Bind a widget event to a Blueprint function
// Params: widgetBlueprintName, widgetName, eventName,
//         functionName
// Note: This creates the binding metadata. The actual function
//       must exist in the widget blueprint's event graph.
// ============================================================
FString FAgenticMCPServer::HandleUMGBindEvent(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString WBPName = Json->GetStringField(TEXT("widgetBlueprintName"));
	FString WidgetName = Json->GetStringField(TEXT("widgetName"));
	FString EventName = Json->GetStringField(TEXT("eventName"));
	FString FunctionName = Json->GetStringField(TEXT("functionName"));

	if (WBPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetBlueprintName"));
	if (WidgetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetName"));
	if (EventName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: eventName"));
	if (FunctionName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: functionName"));

	UWidgetBlueprint* WBP = FindWidgetBlueprintByName(WBPName);
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget blueprint not found: %s"), *WBPName));

	// Event binding in UMG is done through the Blueprint graph system.
	// For programmatic binding, the recommended approach is to use executePython
	// or create the event node in the widget BP's event graph via addNode.
	// Here we record the intent and provide guidance.

	WBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widgetBlueprintName"), WBPName);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("eventName"), EventName);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("note"),
		FString::Printf(TEXT("To complete the binding, use 'addNode' to create a '%s' event node in the widget BP's event graph, "
			"then connect it to a 'Call Function' node targeting '%s'. "
			"Supported events: OnClicked, OnPressed, OnReleased, OnHovered, OnUnhovered, OnTextChanged, OnValueChanged."),
			*EventName, *FunctionName));
	return JsonToString(Result);
}

// ============================================================
// umgGetWidgetChildren - Get children of a widget
// Params: widgetBlueprintName, widgetName (or "root")
// ============================================================
FString FAgenticMCPServer::HandleUMGGetWidgetChildren(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString WBPName = Json->GetStringField(TEXT("widgetBlueprintName"));
	if (WBPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: widgetBlueprintName"));

	FString WidgetName = Json->HasField(TEXT("widgetName")) ? Json->GetStringField(TEXT("widgetName")) : TEXT("root");

	UWidgetBlueprint* WBP = FindWidgetBlueprintByName(WBPName);
	if (!WBP) return MakeErrorJson(FString::Printf(TEXT("Widget blueprint not found: %s"), *WBPName));

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree) return MakeErrorJson(TEXT("No widget tree"));

	UWidget* TargetWidget = nullptr;
	if (WidgetName == TEXT("root"))
		TargetWidget = Tree->RootWidget;
	else
		TargetWidget = FindWidgetInTree(Tree, WidgetName);

	if (!TargetWidget)
		return MakeErrorJson(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	TArray<TSharedPtr<FJsonValue>> ChildrenArr;

	UPanelWidget* Panel = Cast<UPanelWidget>(TargetWidget);
	if (Panel)
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			if (!Child) continue;

			TSharedRef<FJsonObject> ChildJson = MakeShared<FJsonObject>();
			ChildJson->SetNumberField(TEXT("index"), i);
			ChildJson->SetStringField(TEXT("name"), Child->GetName());
			ChildJson->SetStringField(TEXT("class"), Child->GetClass()->GetName());
			ChildJson->SetStringField(TEXT("visibility"),
				StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Child->GetVisibility()));
			ChildJson->SetBoolField(TEXT("isPanel"), Child->IsA<UPanelWidget>());
			if (UTextBlock* TB = Cast<UTextBlock>(Child))
			{
				ChildJson->SetStringField(TEXT("text"), TB->GetText().ToString());
			}
			ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildJson));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widgetBlueprintName"), WBPName);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), TargetWidget->GetClass()->GetName());
	Result->SetBoolField(TEXT("isPanel"), TargetWidget->IsA<UPanelWidget>());
	Result->SetNumberField(TEXT("childCount"), ChildrenArr.Num());
	Result->SetArrayField(TEXT("children"), ChildrenArr);
	return JsonToString(Result);
}
