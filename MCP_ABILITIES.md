# MCP ABILITIES - FULL REFERENCE

## ⚠️ MACHINE PROTOCOL ⚠️

I am a mathematical function. Input → Process → Verify Output → Adjust.

**PROPAGATION IS KING. PROBABILITY INFORMATION > GOLD.**

```
EVERY ACTION REQUIRES:
1. Observe state BEFORE (screenshot/query)
2. Execute action
3. Observe state AFTER
4. COMPARE: Did output match expected?
5. If NO: STOP. Recalculate.
```

`{"success": true}` = command queued. NOT action completed. VERIFY.

---

## MCP Server

- Port: 9847
- Base: http://localhost:9847

---

## Complete Endpoint Reference (107 endpoints)

### Health & Lifecycle
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/health | GET | Server health check |
| /api/shutdown | POST | Shutdown server (commandlet only) |

### Blueprint Read
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list | GET | List all blueprints |
| /api/blueprint | GET | Get blueprint details |
| /api/graph | GET | Get blueprint graph |
| /api/search | GET | Search blueprints |
| /api/references | GET | Find references |
| /api/list-classes | POST | List available classes |
| /api/list-functions | POST | List functions |
| /api/list-properties | POST | List properties |
| /api/get-pin-info | POST | Get node pin info |

### Blueprint Mutation
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/add-node | POST | Add node to graph |
| /api/delete-node | POST | Delete node |
| /api/connect-pins | POST | Connect pins |
| /api/disconnect-pin | POST | Disconnect pin |
| /api/set-pin-default | POST | Set pin default value |
| /api/move-node | POST | Move node position |
| /api/refresh-all-nodes | POST | Refresh all nodes |
| /api/create-blueprint | POST | Create new blueprint |
| /api/create-graph | POST | Create new graph |
| /api/delete-graph | POST | Delete graph |
| /api/add-variable | POST | Add variable |
| /api/remove-variable | POST | Remove variable |
| /api/compile-blueprint | POST | Compile blueprint |
| /api/duplicate-nodes | POST | Duplicate nodes |
| /api/set-node-comment | POST | Set node comment |
| /api/add-component | POST | Add component |

### Actor Management
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list-actors | POST | List all actors |
| /api/get-actor | POST | Get actor details |
| /api/spawn-actor | POST | Spawn new actor |
| /api/delete-actor | POST | Delete actor |
| /api/set-actor-property | POST | Set actor property |
| /api/set-actor-transform | POST | Set actor transform |
| /api/move-actor | POST | Move actor (alias for set-actor-transform) |

### Level Management
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list-levels | POST | List loaded levels and streaming sublevels |
| /api/load-level | POST | Load level by path |
| /api/get-level-blueprint | POST | Get level blueprint |
| /api/remove-sublevel | POST | Remove a streaming sublevel |
| /api/streaming-level-visibility | POST | Show/hide streaming level |
| /api/output-log | POST | Get recent output log entries |

**Streaming Level Visibility:**
```json
POST /api/streaming-level-visibility
{"levelPath": "ML_Main", "visible": true}
// Response: {"success": true, "wasVisible": false, "isNowVisible": true}
```

**Output Log Request:**
```json
POST /api/output-log
{"lines": 50, "filter": "Story"}  // optional filter
// Response: {"lines": [...], "count": 23, "logFile": "path/to/log"}
```

### Asset Editor
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/open-asset | POST | Open asset in editor |

### Validation & Safety
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/validate-blueprint | POST | Validate blueprint |
| /api/snapshot-graph | POST | Snapshot graph state |
| /api/restore-graph | POST | Restore graph state |

### Visual Agent / Automation
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/scene-snapshot | POST | Scene data snapshot |
| /api/screenshot | POST | Capture viewport (base64 PNG) |
| /api/focus-actor | POST | Focus camera on actor |
| /api/select-actor | POST | Select actor |
| /api/set-viewport | POST | Set viewport settings |
| /api/wait-ready | POST | Wait for editor ready |
| /api/resolve-ref | POST | Resolve asset reference |
| /api/get-camera | POST | Get camera info |
| /api/list-viewports | POST | List viewports |
| /api/get-selection | POST | Get selected actors |
| /api/draw-debug | POST | Draw debug shapes |
| /api/clear-debug | POST | Clear debug shapes |
| /api/blueprint-snapshot | POST | Blueprint visual snapshot |

### Transactions & Undo
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/begin-transaction | POST | Begin transaction |
| /api/end-transaction | POST | End transaction |
| /api/undo | POST | Undo |
| /api/redo | POST | Redo |

### State Management
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/save-state | POST | Save state |
| /api/diff-state | POST | Diff state |
| /api/restore-state | POST | Restore state |
| /api/list-states | POST | List states |

### Level Sequences
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list-sequences | POST | List level sequences |
| /api/read-sequence | POST | Read sequence details |
| /api/remove-audio-tracks | POST | Remove audio tracks |

### Python Execution
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/execute-python | POST | Execute Python script |

### PIE Control
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/start-pie | POST | Start Play In Editor |
| /api/stop-pie | POST | Stop PIE |
| /api/pause-pie | POST | Pause PIE |
| /api/step-pie | POST | Step PIE frame |
| /api/get-pie-state | GET | Get PIE status |

### Console Commands
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/execute-console | POST | Run console command |
| /api/get-cvar | GET | Get console variable |
| /api/set-cvar | POST | Set console variable |
| /api/list-cvars | GET | List all console variables |

### Input Simulation
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/simulate-input | POST | Send keyboard/mouse input |

**⚠️ WARNING**: Input simulation returns success when QUEUED, not when PROCESSED.
MUST verify visually with screenshot comparison.

### Audio (10 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/audio/status | GET | Audio system status |
| /api/audio/active-sounds | GET | List active sounds |
| /api/audio/device-info | GET | Audio device info |
| /api/audio/sound-classes | GET | List sound classes |
| /api/audio/set-volume | POST | Set volume |
| /api/audio/stats | GET | Audio stats |
| /api/audio/play | POST | Play sound |
| /api/audio/stop | POST | Stop sound |
| /api/audio/set-listener | POST | Set audio listener |
| /api/audio/debug-visualize | POST | Debug visualization |

### Niagara VFX (11 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/niagara/status | GET | Niagara system status |
| /api/niagara/systems | GET | List Niagara systems |
| /api/niagara/system-info | POST | Get system info |
| /api/niagara/emitters | POST | Get emitters |
| /api/niagara/set-parameter | POST | Set parameter |
| /api/niagara/parameters | POST | Get parameters |
| /api/niagara/activate | POST | Activate system |
| /api/niagara/set-emitter | POST | Enable/disable emitter |
| /api/niagara/reset | POST | Reset system |
| /api/niagara/stats | GET | Niagara stats |
| /api/niagara/debug-hud | POST | Debug HUD |

### PixelStreaming (7 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/pixelstreaming/status | GET | Streaming status |
| /api/pixelstreaming/start | POST | Start streaming |
| /api/pixelstreaming/stop | POST | Stop streaming |
| /api/pixelstreaming/streamers | GET | List streamers |
| /api/pixelstreaming/codec | GET | Get codec |
| /api/pixelstreaming/set-codec | POST | Set codec |
| /api/pixelstreaming/players | GET | List players |

### Meta XR / OculusXR (10 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/xr/status | GET | XR device status |
| /api/xr/guardian | GET | Guardian bounds |
| /api/xr/guardian/visibility | POST | Set guardian visibility |
| /api/xr/hand-tracking | GET | Hand tracking data |
| /api/xr/controllers | GET | Controller state |
| /api/xr/passthrough | GET | Passthrough status |
| /api/xr/passthrough/set | POST | Enable/disable passthrough |
| /api/xr/display-frequency | POST | Set display frequency |
| /api/xr/performance | POST | Set performance levels |
| /api/xr/recenter | POST | Recenter tracking |

### Story/Game (4 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/story/state | GET | Get story state from BP_StoryController |
| /api/story/advance | POST | Advance story to next step |
| /api/story/goto | POST | Jump to specific story index |
| /api/story/play | POST | Play current step's sequence |

**Story handlers search streaming levels for BP_StoryController.**
If controller not found, error message shows which levels were searched.

### DataTable (2 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/datatable/read | POST | Read DataTable |
| /api/datatable/write | POST | Write DataTable |

### Animation (2 endpoints)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/animation/play | POST | Play animation |
| /api/animation/stop | POST | Stop animation |

### Material (1 endpoint)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/material/set-param | POST | Set material parameter |

### Collision (1 endpoint)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/collision/trace | POST | Perform collision trace |

### RenderDoc (1 endpoint)
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/renderdoc/capture | POST | Capture RenderDoc frame |

---

## Editor Control (OS Level)

Kill: `Stop-Process -Name "UnrealEditor" -Force`
Start: `Start-Process "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList "project.uproject"`
Build: `& "$ue5\Engine\Build\BatchFiles\Build.bat" TargetEditor Win64 Development "project.uproject"`

---

## Verification Protocol

Before ANY visual action claim:
```powershell
# 1. Before state
$before = curl -s http://localhost:9847/api/screenshot -X POST -d '{}'

# 2. Action
curl -s http://localhost:9847/api/simulate-input -X POST -d '{"type":"key","key":"W"}'

# 3. After state
$after = curl -s http://localhost:9847/api/screenshot -X POST -d '{}'

# 4. COMPARE - are they different?
# If identical = NOTHING HAPPENED = STOP
```

---

## Common Issues & Solutions

| Problem | Solution |
|---------|----------|
| BP_StoryController not found | Use `/api/streaming-level-visibility` to show ML_Main |
| Streaming level loaded but hidden | Set `visible: true` via streaming-level-visibility |
| Actions fire but nothing happens | Check output log, verify preconditions |
| Screenshot unchanged after action | Action did NOT complete - investigate |

---

## THE 5 VERIFICATION RULES

1. **Screenshot before, screenshot after, compare** - if identical, nothing happened
2. **Wait for action to complete** before taking after screenshot
3. **One action at a time, verify, then proceed** - no rapid-fire
4. **If API says success but screenshot unchanged** - assume failure
5. **Check correct level/context first** - verify preconditions

## RULE 6: FIX BROKEN TOOLS

If API says success but verification shows failure: **FIX THE API.**
I have the power to modify C++ code. I am not a passive consumer of broken tools.

---

## Endpoint Summary by Category

| Category | Count |
|----------|-------|
| Health/Lifecycle | 2 |
| Blueprint Read | 9 |
| Blueprint Mutation | 16 |
| Actor Management | 7 |
| Level Management | 6 |
| Asset Editor | 1 |
| Validation & Safety | 3 |
| Visual Agent | 13 |
| Transactions | 4 |
| State Management | 4 |
| Level Sequences | 3 |
| Python | 1 |
| PIE Control | 5 |
| Console Commands | 4 |
| Input Simulation | 1 |
| Audio | 10 |
| Niagara | 11 |
| PixelStreaming | 7 |
| Meta XR | 10 |
| Story/Game | 4 |
| DataTable | 2 |
| Animation | 2 |
| Material | 1 |
| Collision | 1 |
| RenderDoc | 1 |
| **TOTAL** | **107** |
