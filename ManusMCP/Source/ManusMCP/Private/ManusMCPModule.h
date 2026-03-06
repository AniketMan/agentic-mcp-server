// ManusMCPModule.h
// Module definition for ManusMCP plugin.

#pragma once

#include "Modules/ModuleManager.h"

class FManusMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
