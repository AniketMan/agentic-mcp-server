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

## Endpoints

### PIE Control
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/start-pie | POST | Start Play In Editor |
| /api/stop-pie | POST | Stop PIE |
| /api/pause-pie | POST | Pause PIE |
| /api/get-pie-state | POST | Get current PIE status |

### Level Management
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/load-level | POST | Load level by path |
| /api/list-levels | POST | List loaded levels and streaming sublevels |
| /api/remove-sublevel | POST | Remove a streaming sublevel |
| /api/streaming-level-visibility | POST | Show/hide streaming level |
| /api/get-level-blueprint | POST | Get level blueprint for a level |

**NEW - Streaming Level Visibility:**
```json
POST /api/streaming-level-visibility
{"levelPath": "ML_Main", "visible": true}
// Response: {"success": true, "wasVisible": false, "isNowVisible": true}
```

### Output Log
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/output-log | POST | Get recent output log entries |

**Output Log Request:**
```json
POST /api/output-log
{"lines": 50, "filter": "Story"}  // optional filter
// Response: {"lines": [...], "count": 23, "logFile": "path/to/log"}
```

### Actors
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list-actors | POST | List all actors (includes streaming levels) |
| /api/get-actor | POST | Get actor details |
| /api/spawn-actor | POST | Spawn new actor |
| /api/set-actor-transform | POST | Move/rotate actor |

### Input Simulation
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/simulate-input | POST | Send keyboard/mouse input |

**⚠️ WARNING**: Input simulation returns success when QUEUED, not when PROCESSED.
MUST verify visually with screenshot comparison.

### Console
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/execute-console | POST | Run console command |

### Visual
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/screenshot | POST | Capture viewport (base64 PNG) |

### Sequences
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/list-sequences | POST | List level sequences |
| /api/read-sequence | POST | Read sequence details |

### Audio
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/audio/active-sounds | POST | List active audio |
| /api/audio/play | POST | Play sound |
| /api/audio/stop | POST | Stop sound |

### Story
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/story/state | POST | Get story state from BP_StoryController |
| /api/story/advance | POST | Advance story to next step |
| /api/story/goto | POST | Jump to specific story index |
| /api/story/play | POST | Play current step's sequence |

**Story handlers now search streaming levels for BP_StoryController.**
If controller not found, error message shows which levels were searched.

### Python
| Endpoint | Method | Purpose |
|----------|--------|---------|
| /api/execute-python | POST | Execute Python script |

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
