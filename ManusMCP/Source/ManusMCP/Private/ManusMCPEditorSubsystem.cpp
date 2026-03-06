// ManusMCPEditorSubsystem.cpp
// Implementation of the editor subsystem that hosts the ManusMCP HTTP server.
// The subsystem ticks every editor frame to dequeue and process one HTTP request
// on the game thread, ensuring thread-safe access to UE5 engine APIs.

#include "ManusMCPEditorSubsystem.h"
#include "ManusMCPServer.h"

void UManusMCPEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Don't start in commandlet mode — the commandlet has its own server instance.
	if (IsRunningCommandlet())
	{
		return;
	}

	Server = MakeUnique<FManusMCPServer>();

	// Start in editor mode (disables /api/shutdown to prevent accidental editor kill)
	if (Server->Start(9847, /*bEditorMode=*/true))
	{
		UE_LOG(LogTemp, Display,
			TEXT("ManusMCP: Editor subsystem started - HTTP server on port %d (%d Blueprints, %d Maps)"),
			Server->GetPort(), Server->GetBlueprintCount(), Server->GetMapCount());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ManusMCP: Editor subsystem failed to start HTTP server (port 9847 may be in use)"));
		Server.Reset();
	}
}

void UManusMCPEditorSubsystem::Deinitialize()
{
	if (Server)
	{
		Server->Stop();
		Server.Reset();
		UE_LOG(LogTemp, Display, TEXT("ManusMCP: Editor subsystem stopped."));
	}

	Super::Deinitialize();
}

void UManusMCPEditorSubsystem::Tick(float DeltaTime)
{
	if (Server)
	{
		// Process up to 4 requests per tick to improve throughput
		// while keeping frame time impact minimal
		for (int32 i = 0; i < 4; ++i)
		{
			if (!Server->ProcessOneRequest())
			{
				break; // No more pending requests
			}
		}
	}
}

bool UManusMCPEditorSubsystem::IsTickable() const
{
	return Server.IsValid() && Server->IsRunning();
}

TStatId UManusMCPEditorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UManusMCPEditorSubsystem, STATGROUP_Tickables);
}
