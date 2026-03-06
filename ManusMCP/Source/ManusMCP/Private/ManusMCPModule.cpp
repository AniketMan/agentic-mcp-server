// ManusMCPModule.cpp
// Module startup/shutdown for ManusMCP plugin.
// The actual server lifecycle is managed by the EditorSubsystem or Commandlet,
// not by the module itself. This keeps the module lightweight.

#include "ManusMCPModule.h"

#define LOCTEXT_NAMESPACE "FManusMCPModule"

void FManusMCPModule::StartupModule()
{
	UE_LOG(LogTemp, Display, TEXT("ManusMCP: Module loaded."));
}

void FManusMCPModule::ShutdownModule()
{
	UE_LOG(LogTemp, Display, TEXT("ManusMCP: Module unloaded."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FManusMCPModule, ManusMCP)
