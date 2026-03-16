// Handlers_RenderDoc.cpp
// RenderDoc capture handler for AgenticMCP.
// Provides endpoints to trigger RenderDoc frame captures via console commands.

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

// ============================================================
// HandleRenderDocCapture
// POST /api/renderdoc/capture { "frameCount": 1 }
// Triggers a RenderDoc frame capture using console commands.
// Requires RenderDoc to be attached to the UE process.
// ============================================================

FString FAgenticMCPServer::HandleRenderDocCapture(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);

	int32 FrameCount = 1;
	if (Json.IsValid() && Json->HasField(TEXT("frameCount")))
	{
		FrameCount = FMath::Clamp(Json->GetIntegerField(TEXT("frameCount")), 1, 10);
	}

	// Try to trigger RenderDoc capture via console command
	// This requires RenderDoc to be attached to the process
	if (GEditor)
	{
		// The renderdoc.CaptureFrame console command triggers a capture
		// when RenderDoc is attached
		for (int32 i = 0; i < FrameCount; ++i)
		{
			GEditor->Exec(GEditor->GetEditorWorldContext().World(), TEXT("renderdoc.CaptureFrame"));
		}
	}
	else
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("frameCount"), FrameCount);
	OutJson->SetStringField(TEXT("note"), TEXT("RenderDoc capture command sent. Ensure RenderDoc is attached to the Unreal process."));

	return JsonToString(OutJson);
}
