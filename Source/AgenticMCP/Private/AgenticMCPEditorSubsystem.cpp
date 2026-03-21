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

// Port range for auto-discovery when multiple editor instances are running.
// The Node.js bridge reads UNREAL_MCP_URL or defaults to http://localhost:9847.
// When running two projects (e.g. MCPLEVEL + OrdinaryCourage), the first editor
// claims 9847 and the second auto-increments to 9848, etc.
static constexpr int32 MCP_PORT_BASE = 9847;
static constexpr int32 MCP_PORT_MAX  = 9857;

void UAgenticMCPEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Don't start in commandlet mode -- the commandlet has its own server instance.
	if (IsRunningCommandlet())
	{
		return;
	}

	Server = MakeUnique<FAgenticMCPServer>();

	// Try ports in range until one succeeds (handles multiple editor instances)
	bool bStarted = false;
	for (int32 Port = MCP_PORT_BASE; Port <= MCP_PORT_MAX; ++Port)
	{
		if (Server->Start(Port, /*bEditorMode=*/true))
		{
			UE_LOG(LogTemp, Display,
				TEXT("AgenticMCP: Editor subsystem started - HTTP server on port %d (%d Blueprints, %d Maps)"),
				Server->GetPort(), Server->GetBlueprintCount(), Server->GetMapCount());

			if (Port != MCP_PORT_BASE)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("AgenticMCP: Default port %d was in use. Bound to port %d instead. ")
					TEXT("Set UNREAL_MCP_URL=http://localhost:%d for the Node.js bridge."),
					MCP_PORT_BASE, Port, Port);
			}

			bStarted = true;
			break;
		}

		UE_LOG(LogTemp, Log,
			TEXT("AgenticMCP: Port %d in use, trying %d..."), Port, Port + 1);
	}

	if (!bStarted)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: Failed to start HTTP server on any port in range %d-%d. All ports may be in use."),
			MCP_PORT_BASE, MCP_PORT_MAX);
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
