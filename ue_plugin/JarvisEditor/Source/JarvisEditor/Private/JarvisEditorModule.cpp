// Copyright Aniket Bhatt. All Rights Reserved.
// JarvisEditorModule.cpp - Module startup/shutdown for JARVIS Editor plugin

#include "JarvisEditorModule.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FJarvisEditorModule"

// Custom log category for all JARVIS operations
// Usage: UE_LOG(LogJarvis, Log, TEXT("message"));
// Filter in Output Log with "LogJarvis" to see only JARVIS messages
DEFINE_LOG_CATEGORY_STATIC(LogJarvis, Log, All);

void FJarvisEditorModule::StartupModule()
{
	UE_LOG(LogJarvis, Log, TEXT("=== JARVIS Editor Module Starting ==="));
	UE_LOG(LogJarvis, Log, TEXT("Blueprint graph manipulation functions registered."));
	UE_LOG(LogJarvis, Log, TEXT("Python-callable functions available via UJarvisBlueprintLibrary."));
	UE_LOG(LogJarvis, Log, TEXT("=== JARVIS Ready ==="));

	RegisterConsoleCommands();
}

void FJarvisEditorModule::ShutdownModule()
{
	UE_LOG(LogJarvis, Log, TEXT("=== JARVIS Editor Module Shutting Down ==="));
}

void FJarvisEditorModule::RegisterConsoleCommands()
{
	// Console commands for quick testing without Python
	// Usage in editor console: JarvisEditor.Status
	// These are convenience wrappers - the real API is through UJarvisBlueprintLibrary
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FJarvisEditorModule, JarvisEditor)
