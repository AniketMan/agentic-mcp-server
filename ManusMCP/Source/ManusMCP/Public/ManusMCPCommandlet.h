// ManusMCPCommandlet.h
// Standalone commandlet for running the ManusMCP server without the editor.
// Usage: UnrealEditor-Cmd.exe YourProject.uproject -run=ManusMCP -port=9847

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ManusMCPCommandlet.generated.h"

/**
 * UManusMCPCommandlet
 *
 * Runs the ManusMCP HTTP server in a headless UnrealEditor-Cmd process.
 * Useful for CI/CD pipelines or when the editor is not open.
 *
 * The commandlet manually ticks the engine to process HTTP requests.
 * It runs until /api/shutdown is called or the process is killed.
 */
UCLASS()
class UManusMCPCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UManusMCPCommandlet();

	virtual int32 Main(const FString& Params) override;
};
