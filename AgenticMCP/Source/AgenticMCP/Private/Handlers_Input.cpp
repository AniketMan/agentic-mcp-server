// Handlers_Input.cpp
// Input simulation handlers for AgenticMCP
// Allows sending keyboard/mouse input to PIE session

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

FString FAgenticMCPServer::HandleSimulateInput(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString InputType = BodyJson->GetStringField(TEXT("type")); // "key", "mouse"

	if (InputType.Equals(TEXT("key"), ESearchCase::IgnoreCase))
	{
		FString KeyName = BodyJson->GetStringField(TEXT("key"));
		FString Action = BodyJson->HasField(TEXT("action")) ? BodyJson->GetStringField(TEXT("action")) : TEXT("press");

		FKey Key = FKey(*KeyName);
		if (!Key.IsValid())
		{
			// Try common key mappings
			if (KeyName.Equals(TEXT("Space"), ESearchCase::IgnoreCase)) Key = EKeys::SpaceBar;
			else if (KeyName.Equals(TEXT("Enter"), ESearchCase::IgnoreCase)) Key = EKeys::Enter;
			else if (KeyName.Equals(TEXT("Escape"), ESearchCase::IgnoreCase)) Key = EKeys::Escape;
			else if (KeyName.Equals(TEXT("Shift"), ESearchCase::IgnoreCase)) Key = EKeys::LeftShift;
			else if (KeyName.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase)) Key = EKeys::LeftControl;
			else if (KeyName.Equals(TEXT("Alt"), ESearchCase::IgnoreCase)) Key = EKeys::LeftAlt;
			else if (KeyName.Equals(TEXT("Tab"), ESearchCase::IgnoreCase)) Key = EKeys::Tab;
			else if (KeyName.Equals(TEXT("W"), ESearchCase::IgnoreCase)) Key = EKeys::W;
			else if (KeyName.Equals(TEXT("A"), ESearchCase::IgnoreCase)) Key = EKeys::A;
			else if (KeyName.Equals(TEXT("S"), ESearchCase::IgnoreCase)) Key = EKeys::S;
			else if (KeyName.Equals(TEXT("D"), ESearchCase::IgnoreCase)) Key = EKeys::D;
			else if (KeyName.Equals(TEXT("E"), ESearchCase::IgnoreCase)) Key = EKeys::E;
			else if (KeyName.Equals(TEXT("Q"), ESearchCase::IgnoreCase)) Key = EKeys::Q;
			else if (KeyName.Equals(TEXT("F"), ESearchCase::IgnoreCase)) Key = EKeys::F;
			else if (KeyName.Equals(TEXT("R"), ESearchCase::IgnoreCase)) Key = EKeys::R;
			else if (KeyName.Equals(TEXT("LeftMouseButton"), ESearchCase::IgnoreCase)) Key = EKeys::LeftMouseButton;
			else if (KeyName.Equals(TEXT("RightMouseButton"), ESearchCase::IgnoreCase)) Key = EKeys::RightMouseButton;
			else
			{
				return MakeErrorJson(FString::Printf(TEXT("Unknown key: %s"), *KeyName));
			}
		}

		// Get the Slate application to send input
		FSlateApplication& SlateApp = FSlateApplication::Get();

		FModifierKeysState ModifierKeys;

		if (Action.Equals(TEXT("press"), ESearchCase::IgnoreCase))
		{
			// Simulate key down then key up
			FKeyEvent KeyDownEvent(Key, ModifierKeys, 0, false, 0, 0);
			SlateApp.ProcessKeyDownEvent(KeyDownEvent);

			FKeyEvent KeyUpEvent(Key, ModifierKeys, 0, false, 0, 0);
			SlateApp.ProcessKeyUpEvent(KeyUpEvent);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("action"), TEXT("press"));
		}
		else if (Action.Equals(TEXT("down"), ESearchCase::IgnoreCase))
		{
			FKeyEvent KeyDownEvent(Key, ModifierKeys, 0, false, 0, 0);
			SlateApp.ProcessKeyDownEvent(KeyDownEvent);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("action"), TEXT("down"));
		}
		else if (Action.Equals(TEXT("up"), ESearchCase::IgnoreCase))
		{
			FKeyEvent KeyUpEvent(Key, ModifierKeys, 0, false, 0, 0);
			SlateApp.ProcessKeyUpEvent(KeyUpEvent);

			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("action"), TEXT("up"));
		}

		Result->SetStringField(TEXT("key"), Key.ToString());
	}
	else if (InputType.Equals(TEXT("mouse"), ESearchCase::IgnoreCase))
	{
		// Mouse click simulation
		float X = BodyJson->HasField(TEXT("x")) ? BodyJson->GetNumberField(TEXT("x")) : 0;
		float Y = BodyJson->HasField(TEXT("y")) ? BodyJson->GetNumberField(TEXT("y")) : 0;
		FString Button = BodyJson->HasField(TEXT("button")) ? BodyJson->GetStringField(TEXT("button")) : TEXT("left");

		FSlateApplication& SlateApp = FSlateApplication::Get();

		FVector2D Position(X, Y);
		FPointerEvent MouseEvent(
			0, // PointerIndex
			Position,
			Position,
			Button.Equals(TEXT("left"), ESearchCase::IgnoreCase) ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>{EKeys::RightMouseButton},
			Button.Equals(TEXT("left"), ESearchCase::IgnoreCase) ? EKeys::LeftMouseButton : EKeys::RightMouseButton,
			0, // WheelDelta
			FModifierKeysState()
		);

		SlateApp.ProcessMouseButtonDownEvent(nullptr, MouseEvent);
		SlateApp.ProcessMouseButtonUpEvent(MouseEvent);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("action"), TEXT("click"));
		Result->SetNumberField(TEXT("x"), X);
		Result->SetNumberField(TEXT("y"), Y);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown input type: %s. Use 'key' or 'mouse'"), *InputType));
	}

	return JsonToString(Result);
}
