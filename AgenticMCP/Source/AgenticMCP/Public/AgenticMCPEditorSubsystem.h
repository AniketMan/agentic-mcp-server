// AgenticMCPEditorSubsystem.h
// Editor subsystem that hosts the AgenticMCP HTTP server inside the running UE5 editor.
// Requests are dequeued and processed on the editor's game thread via FTickableEditorObject::Tick().

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Tickable.h"
#include "AgenticMCPServer.h"
#include "AgenticMCPEditorSubsystem.generated.h"

/**
 * UAgenticMCPEditorSubsystem
 *
 * Automatically starts the AgenticMCP HTTP server when the UE5 editor opens.
 * The MCP TypeScript wrapper connects to this server over localhost:9847.
 *
 * This is the preferred serving mode — zero extra RAM, instant startup,
 * full access to all editor APIs including level management.
 */
UCLASS()
class UAgenticMCPEditorSubsystem : public UEditorSubsystem, public FTickableEditorObject
{
	GENERATED_BODY()

public:
	// UEditorSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	/** The HTTP server instance. Null if startup failed. */
	TUniquePtr<FAgenticMCPServer> Server;
};
