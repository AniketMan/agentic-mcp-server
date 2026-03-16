// AgenticMCPEditorSubsystem.cpp
// Implementation of the editor subsystem that hosts the AgenticMCP HTTP server.
// The subsystem ticks every editor frame to dequeue and process one HTTP request
// on the game thread, ensuring thread-safe access to UE5 engine APIs.

#include "AgenticMCPEditorSubsystem.h"
#include "AgenticMCPServer.h"

// FIX: FScopedSlowTask REMOVED entirely.
// The destructor of FScopedSlowTask calls FText::Rebuild() which crashes
// with EXCEPTION_ACCESS_VIOLATION (null at offset 0xa8) after level blueprint
// mutations (addNode on LevelScriptBlueprint). The mutation invalidates the
// FText internals during ProcessOneRequest(), and when the FScopedSlowTask
// destructor runs at the closing brace, it dereferences the now-null FText.
//
// The root FScopedSlowTask was originally added to provide a parent scope
// for child FSlowTasks created by engine subsystems (Blueprint compile,
// level load, asset streaming). Without it, those child tasks also crash.
//
// NEW APPROACH: Wrap each ProcessOneRequest() call in a Windows SEH guard.
// If a child FSlowTask crashes, we catch it and continue processing the
// next request instead of taking down the editor. No parent FSlowTask needed.

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"

static bool ProcessOneRequestSEH(FAgenticMCPServer* Server)
{
	__try
	{
		return Server->ProcessOneRequest();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AgenticMCP: SEH exception caught in ProcessOneRequest() (code 0x%08X). ")
			TEXT("This typically occurs after level blueprint mutations. The request was skipped."),
			GetExceptionCode());
		return true; // Return true to continue processing (there may be more requests)
	}
}

#include "Windows/HideWindowsPlatformTypes.h"
#else
// Non-Windows: no SEH, just call directly
static bool ProcessOneRequestSEH(FAgenticMCPServer* Server)
{
	return Server->ProcessOneRequest();
}
#endif

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
		// Process up to 4 requests per tick to improve throughput
		// while keeping frame time impact minimal.
		// Each call is SEH-guarded so a crash in one request
		// does not take down the editor or block subsequent requests.
		for (int32 i = 0; i < 4; ++i)
		{
			if (!ProcessOneRequestSEH(Server.Get()))
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
