// Copyright Aniket Bhatt. All Rights Reserved.
// JarvisEditorModule.h - Module interface for JARVIS Editor plugin

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * FJarvisEditorModule
 * 
 * Editor module that registers Blueprint graph manipulation functions
 * exposed to Python scripting via UFUNCTION(BlueprintCallable).
 * 
 * This enables AI-controlled editing of:
 * - Blueprint graphs (add/remove/connect nodes)
 * - Level Script Blueprints
 * - Level Sequences
 * - Actor properties and components
 * 
 * All functions include comprehensive logging for troubleshooting.
 */
class FJarvisEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Register console commands for direct testing */
	void RegisterConsoleCommands();
};
