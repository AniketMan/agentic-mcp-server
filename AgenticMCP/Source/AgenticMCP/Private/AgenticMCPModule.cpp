// AgenticMCPModule.cpp
// Module startup/shutdown for AgenticMCP plugin.
// The actual server lifecycle is managed by the EditorSubsystem or Commandlet,
// not by the module itself. This keeps the module lightweight.

#include "AgenticMCPModule.h"

#define LOCTEXT_NAMESPACE "FAgenticMCPModule"

void FAgenticMCPModule::StartupModule()
{
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Module loaded."));
}

void FAgenticMCPModule::ShutdownModule()
{
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Module unloaded."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgenticMCPModule, AgenticMCP)
