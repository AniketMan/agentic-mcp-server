// AgenticMCPEditorSubsystem.cpp
// Implementation of the editor subsystem that hosts the AgenticMCP HTTP server.
// The subsystem ticks every editor frame to dequeue and process one HTTP request
// on the game thread, ensuring thread-safe access to UE5 engine APIs.

#include "AgenticMCPEditorSubsystem.h"
#include "AgenticMCPServer.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"

void UAgenticMCPEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Don't start in commandlet mode -- the commandlet has its own server instance.
	if (IsRunningCommandlet())
	{
		return;
	}

	Server = MakeUnique<FAgenticMCPServer>();

	// Start in editor mode (disables /api/shutdown to prevent accidental editor kill)
	if (Server->Start(9847, /*bEditorMode=*/true))
	{
		UE_LOG(LogTemp, Display,
			TEXT("AgenticMCP: Editor subsystem started - HTTP server on port %d (%d Blueprints, %d Maps)"),
			Server->GetPort(), Server->GetBlueprintCount(), Server->GetMapCount());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: Editor subsystem failed to start HTTP server (port 9847 may be in use)"));
		Server.Reset();
	}
}

void UAgenticMCPEditorSubsystem::Deinitialize()
{
	if (Server)
	{
		Server->Stop();
		Server.Reset();
		UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Editor subsystem stopped."));
	}

	Super::Deinitialize();
}

void UAgenticMCPEditorSubsystem::Tick(float DeltaTime)
{
	if (Server)
	{
		// FIX: Create a root FScopedSlowTask scope for ALL MCP operations.
		// Without this, any engine code that internally creates an FSlowTask
		// (Blueprint compilation, level loading, asset streaming) will crash
		// with EXCEPTION_ACCESS_VIOLATION in FText::Rebuild() because the
		// FSlowTask system expects a valid parent FText scope, which doesn't
		// exist when called from Tick() context.
		//
		// This single scope covers ALL handlers dispatched during this tick.
		// Child FSlowTasks created by engine subsystems inherit this scope
		// and get a valid FText instead of hitting null.
		//
		// FIX 2: Use FText::FromString instead of NSLOCTEXT to avoid
		// FText::Rebuild() crash. NSLOCTEXT holds a reference to the
		// localization table which can be invalidated during
		// ProcessOneRequest() (Blueprint compile, level load, asset stream).
		// FText::FromString owns its string data directly -- no dangling ref.
		static const FText MCPStatusText = FText::FromString(TEXT("AgenticMCP Processing..."));
		FScopedSlowTask RootSlowTask(0.0f, MCPStatusText);

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

bool UAgenticMCPEditorSubsystem::IsTickable() const
{
	return Server.IsValid() && Server->IsRunning();
}

TStatId UAgenticMCPEditorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAgenticMCPEditorSubsystem, STATGROUP_Tickables);
}
