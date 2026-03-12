# SOH VR -- Complete Execution Plan v2

**Date:** March 11, 2026
**Based on:** Live Perforce audit of `//SOH_VR/new-main` (3,538 files, CL 34337)

---

## What Already Exists in Your Project

The Perforce audit revealed that you already have two working plugins in the project:

**DevmateMCP** (added CL 34288, March 6) is a lightweight HTTP server running inside the UE5 editor with 7 endpoints: health check, list blueprints, list actors, execute Python, run the interaction injector, create a VR playground, and get the current editor selection. It is already compiled and running in your project.

**JarvisEditor / SOHInteractionInjector** (966 lines of C++) is a hardcoded interaction chain injector that defines all interaction-to-step mappings for Scenes 1 through 8 (Steps 1 through 34). It creates 4-node Blueprint chains (CustomEvent, GetSubsystem, BroadcastMessage, MakeStruct), wires all pins programmatically, compiles the Blueprint, and supports both dry-run mode and undo transactions. The `JarvisBlueprintLibrary` helper class provides proven functions for `AddCustomEventNode`, `AddGetSubsystemNode`, `AddCallFunctionNode`, `AddMakeStructNode`, `ConnectPins`, `SetPinDefaultValue`, `CompileBlueprint`, `BeginTransaction`, and `EndTransaction`.

**The trigger wiring section (lines 947-966) is marked TODO.** The injector creates the CustomEvent chains and the BroadcastMessage wiring, but the connection from in-world actors (GrabbableComponent.OnInteractionStart, TeleportPoint.OnPlayerTeleported) to those CustomEvents is not yet implemented. This is likely where the AI "went rogue" -- it tried to complete this incomplete logic and made bad assumptions about pin names and node types.

---

## What AgenticMCP Adds Over DevmateMCP

AgenticMCP (the `agentic-mcp-server` repo) has 130 HTTP endpoints compared to DevmateMCP's 7. The critical additions are:

| Capability | DevmateMCP | AgenticMCP |
|---|---|---|
| List Blueprints | Yes | Yes + search, references, classes, functions, properties |
| Create Blueprints | No | Yes (createBlueprint) |
| Add/Delete Nodes | No | Yes (22 node types) |
| Connect/Disconnect Pins | No | Yes (connectPins, disconnectPin) |
| Set Pin Defaults | No | Yes (setPinDefault) |
| Add Variables/Components | No | Yes |
| Compile Blueprints | No | Yes |
| Undo/Redo/Snapshots | No | Yes (full transaction system) |
| Spawn/Delete Actors | No | Yes |
| Level Sequences | No | Yes (read, list, remove audio tracks) |
| PIE Control | No | Yes (start, stop, pause, step) |
| VR/XR Status | No | Yes (guardian, hand tracking, controllers) |
| Niagara | No | Yes (10 endpoints) |
| Audio | No | Yes (10 endpoints) |
| Materials | No | Yes |
| Scene Snapshot | No | Yes (full actor/component dump) |
| MCP Protocol Bridge | No | Yes (Claude Desktop integration) |

However, DevmateMCP has one thing AgenticMCP does not: the `SOHInteractionInjector` with all 34 hardcoded interaction definitions specific to your project. That data is gold.

---

## The Plan: What to Do Today

### Decision: Use DevmateMCP + AgenticMCP Together

Do not replace DevmateMCP. Keep it running on its current port. Add AgenticMCP on port 9847. The two plugins coexist. DevmateMCP gives Claude the SOH-specific interaction map via `/api/run-injector`. AgenticMCP gives Claude the full Blueprint manipulation toolkit.

### Step 1: Pull the Latest AgenticMCP Repo (2 minutes)

```powershell
cd E:\JARVIS\EVE
git -C agentic-mcp-server pull
```

This gets the quantizer tool and the updated docs with confidence thresholds.

### Step 2: Copy AgenticMCP Plugin into SOH Project (5 minutes)

```powershell
$SOH = "D:\SOH_VR"  # CHANGE THIS to your actual SOH project path

# Create the plugin directory
New-Item -ItemType Directory -Force -Path "$SOH\Plugins\AgenticMCP"

# Copy plugin descriptor and source
Copy-Item -Force "E:\JARVIS\EVE\agentic-mcp-server\AgenticMCP.uplugin" "$SOH\Plugins\AgenticMCP\"
Copy-Item -Recurse -Force "E:\JARVIS\EVE\agentic-mcp-server\Source" "$SOH\Plugins\AgenticMCP\"
```

**IMPORTANT:** The AgenticMCP `Build.cs` depends on `OculusXRHMD` and `OculusXRInput`. Your project already has `MetaXRAudio`, `MetaXRHaptics`, and `OculusXRMovement` plugins, so the Meta XR SDK is present. Verify that `OculusXRHMD` and `OculusXRInput` modules are available. If not, either install the full Meta XR Plugin or comment out the XR handler registrations in `AgenticMCPServer.cpp` and remove those modules from `Build.cs`.

### Step 3: Submit to Perforce (2 minutes)

```powershell
# In your P4V client or PowerShell with p4 configured:
p4 add "$SOH\Plugins\AgenticMCP\..."
p4 submit -d "Add AgenticMCP plugin (130 endpoints for AI-driven Blueprint manipulation)"
```

### Step 4: Open UE5 and Verify Both Plugins (5 minutes)

Open the SOH project in UE5. Watch the Output Log for:

```
DevmateMCP: HTTP server started on port 8080
AgenticMCP: Editor subsystem started - HTTP server on port 9847
```

Test both:

```powershell
# DevmateMCP
Invoke-RestMethod -Uri "http://localhost:8080/api/health"

# AgenticMCP
Invoke-RestMethod -Uri "http://localhost:9847/api/health"
```

### Step 5: Install the MCP Bridge for Claude Desktop (5 minutes)

```powershell
cd E:\JARVIS\EVE\agentic-mcp-server\AgenticMCP\Tools
npm install
```

Add to Claude Desktop config (`%APPDATA%\Claude\claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "agenticmcp": {
      "command": "node",
      "args": ["E:\\JARVIS\\EVE\\agentic-mcp-server\\AgenticMCP\\Tools\\index.js"],
      "env": {
        "UE_HOST": "localhost",
        "UE_PORT": "9847"
      }
    }
  }
}
```

Restart Claude Desktop. The MCP tools icon (hammer) should appear.

### Step 6: Run the Injector First (5 minutes)

Before Claude starts wiring anything new, run the existing injector to lay down all 34 interaction chains:

```powershell
# Dry run first to see what it would do
Invoke-RestMethod -Method POST -Uri "http://localhost:8080/api/run-injector" -Body '{"dryRun": true}' -ContentType "application/json"

# If dry run looks correct, run live
Invoke-RestMethod -Method POST -Uri "http://localhost:8080/api/run-injector" -Body '{"dryRun": false}' -ContentType "application/json"
```

This creates all CustomEvent chains for Scenes 1-8. The trigger wiring (connecting in-world actors to those events) is still TODO.

### Step 7: Use Claude to Complete the Trigger Wiring (the main event)

This is where AgenticMCP earns its keep. The SOHInteractionInjector created the CustomEvent chains but left the trigger wiring as TODO. Claude, using AgenticMCP's full Blueprint manipulation tools, can now complete that wiring.

Send Claude this prompt:

```
I have two MCP plugins running in my UE5 editor:
- DevmateMCP on port 8080 (SOH-specific, has run-injector)
- AgenticMCP on port 9847 (full Blueprint manipulation)

The SOHInteractionInjector has already created CustomEvent chains in each
level's Logic sublevel. Each CustomEvent fires a BroadcastMessage on the
"Message.Event.StoryStep" channel with a Step integer.

YOUR TASK: Complete the trigger wiring that connects in-world actors to
these CustomEvents. The pattern for each interaction is:

For GRIP interactions:
  BeginPlay -> GetActorByLabel("ActorName") -> GetComponentByClass(GrabbableComponent)
  -> BindEvent(OnGrabbed) -> CustomEvent("EventName")

For NAV interactions:
  BeginPlay -> GetActorByLabel("BP_TeleportPoint") -> BindEvent(OnPlayerTeleported)
  -> CustomEvent("EventName")

Here are the interactions that need trigger wiring:

MAIN (SL_Main_Logic):
- Scene1_HeatherPhotoGazed: GAZE at SM_Picture_Frame -> Step 2
- Scene1_HeatherHugged: GRIP BP_HeatherChild -> Step 3
- Scene2_KitchenTableNav: NAV BP_TeleportPoint -> Step 4
- Scene2_IllustrationGrabbed: GRIP SM_IllustrationPaper -> Step 5

TRAILER (SL_Trailer_Logic):
- Scene3_DoorKnobGrabbed: GRIP BP_Door -> Step 7
- Scene3_FridgeDoorGrabbed: GRIP BP_Door -> Step 8
- Scene3_PitcherGrabbed: GRIP Grabbable_SmallCube -> Step 9
- Scene3_PitcherComplete: OVERLAP Grabbable_SmallCube -> Step 10
- Scene4_PhoneGrabbed: GRIP Grabbable_SmallCube -> Step 11
- Scene4_TextAdvance1: GRIP Grabbable_SmallCube -> Step 12
- Scene4_TextAdvance2: GRIP Grabbable_SmallCube -> Step 13
- Scene4_DoorHandleGrabbed: GRIP BP_Door -> Step 14

BEFORE making any changes:
1. Call snapshot_graph on each Logic sublevel Blueprint
2. List all existing CustomEvent nodes to verify the injector ran
3. List all actors in each level to verify the trigger actors exist
4. Tell me your plan and confidence level for each wiring operation
5. Do NOT execute until I approve

Use AgenticMCP tools (port 9847) for all Blueprint manipulation.
```

### Step 8: Review and Iterate

For each level:
1. Claude proposes the wiring plan with confidence scores
2. You approve or adjust
3. Claude executes using AgenticMCP tools
4. You verify in the UE5 editor
5. If something is wrong, Claude calls `restore_graph` to undo

---

## Project Metrics from Perforce

| Metric | Value |
|---|---|
| Total files | 3,538 |
| UE assets (.uasset) | 3,140 |
| Level files (.umap) | 63 |
| C++ source files | 228 (121 .h + 107 .cpp) |
| Level Sequences | ~50 across 9 scenes |
| Game levels | 7 (Main, Trailer, Hospital, Restaurant, Scene6, Scene8, Scene_9) |
| Test levels | 18 |
| Team members | 2 (aniketbhatt, ricardotorres) |
| Latest CL | 34337 (March 11, 2026) |
| Engine | UE5.6 Oculus Fork with GPU Lightmass |

## Scene-by-Interaction Map (from SOHInteractionInjector)

| Scene | Level | Interactions | Steps | Status |
|---|---|---|---|---|
| 1: Larger Than Life | SL_Main_Logic | 2 (Gaze, Grip) | 2-3 | Chains created, triggers TODO |
| 2: Standing Up For Others | SL_Main_Logic | 2 (Nav, Grip) | 4-5 | Chains created, triggers TODO |
| 3: Rescuers | SL_Trailer_Logic | 4 (Grip x3, Overlap) | 7-10 | Chains created, triggers TODO |
| 4: Stepping Into Adulthood | SL_Trailer_Logic | 4 (Grip x3, Grip) | 11-14 | Chains created, triggers TODO |
| 5: Holding On | SL_Hospital_Logic | TBD | 15+ | Chains created, triggers TODO |
| 6: Letting Go | SL_Restaurant_Logic | TBD | TBD | Chains created, triggers TODO |
| 7: The Investigation | SL_Scene6_Logic | 6 (Grip, Nav, Timer x2, Nav, Timer) | 24-29 | Chains created, triggers TODO |
| 8: Turning Grief Into Action | SL_TrailerScene8_Logic | 5 (Grip x5) | 30-34 | Chains created, triggers TODO |
| 9: TBD | ML_Scene9_Logic | TBD | TBD | Not in injector |

---

## Risk Assessment

| Risk | Likelihood | Mitigation |
|---|---|---|
| AgenticMCP fails to compile (OculusXR deps) | Medium | Comment out XR handlers, remove from Build.cs |
| Pin names differ from what Claude expects | High | SOHInteractionInjector already documents verified pin names. Claude should call `get_pin_info` before wiring. |
| Claude wires incorrectly | Medium | Snapshot system provides instant undo. Require approval before execution. |
| Port conflict between DevmateMCP and AgenticMCP | Low | Different ports (8080 vs 9847) |
| Restaurant scene corruption (3 reverts last week) | Medium | Always snapshot before touching SL_Restaurant_Logic |
