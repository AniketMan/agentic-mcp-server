// AgenticMCPModule.h
// Module definition for AgenticMCP plugin.

#pragma once

#include "Modules/ModuleManager.h"

class FAgenticMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
