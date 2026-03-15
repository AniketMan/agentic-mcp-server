// AgenticMCPCommandlet.cpp
// Standalone commandlet that runs the AgenticMCP HTTP server in a headless process.
// This allows AI tools to manipulate Blueprints without the editor GUI.
//
// Usage:
//   UnrealEditor-Cmd.exe YourProject.uproject -run=AgenticMCP [-port=9847]
//
// The commandlet blocks in a tick loop until /api/shutdown is called.

#include "AgenticMCPCommandlet.h"
#include "AgenticMCPServer.h"
#include "Misc/Parse.h"

UAgenticMCPCommandlet::UAgenticMCPCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAgenticMCPCommandlet::Main(const FString& Params)
{
	// Parse port from command line (default: 9847)
	int32 Port = 9847;
	FParse::Value(*Params, TEXT("-port="), Port);

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Commandlet starting on port %d..."), Port);

	// Create and start the server in commandlet mode (enables /api/shutdown)
	FAgenticMCPServer Server;
	if (!Server.Start(Port, /*bEditorMode=*/false))
	{
		UE_LOG(LogTemp, Error, TEXT("AgenticMCP: Failed to start HTTP server on port %d. Exiting."), Port);
		return 1;
	}

	UE_LOG(LogTemp, Display,
		TEXT("AgenticMCP: Commandlet ready - port %d (%d Blueprints, %d Maps). Waiting for requests..."),
		Port, Server.GetBlueprintCount(), Server.GetMapCount());

	// Main tick loop -- process requests until shutdown is requested
	// We tick at ~60Hz to keep response latency low without burning CPU
	const double TickInterval = 1.0 / 60.0;

	while (!IsEngineExitRequested())
	{
		double TickStart = FPlatformTime::Seconds();

		// Tick the engine (required for async operations, garbage collection, etc.)
		FTSTicker::GetCoreTicker().Tick(TickInterval);

		// Process pending HTTP requests on this (game) thread
		Server.ProcessOneRequest();

		// Sleep to maintain target tick rate
		double Elapsed = FPlatformTime::Seconds() - TickStart;
		if (Elapsed < TickInterval)
		{
			FPlatformProcess::Sleep(static_cast<float>(TickInterval - Elapsed));
		}
	}

	// Clean shutdown
	Server.Stop();
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Commandlet shut down cleanly."));

	return 0;
}
