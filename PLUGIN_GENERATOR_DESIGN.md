# AgenticMCP Architecture

## Overview

AgenticMCP is a **two-tier plugin system** for Unreal Engine that gives AI agents full control over the editor.

**IMPORTANT:** This is a **TOOL PROVIDER**, not an AI invoker.

The AI lives in the **host application** (VS Code + Cline/Devmate, Claude Desktop, Cursor, or a custom in-house tool). We just expose MCP tools and project context. No API keys needed in our plugins.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     AI HOST (provides the AI)                            │
│                                                                          │
│   • VS Code + Cline / Copilot / Devmate                                 │
│   • Claude Desktop                                                       │
│   • Cursor                                                              │
│   • Windsurf                                                            │
│   • Custom in-house tool                                                │
│                                                                          │
│   THEY handle AI API keys and billing. WE don't.                        │
│                                                                          │
└──────────────────────────────────┬──────────────────────────────────────┘
                                   │
                          MCP Protocol (stdio)
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              {ProjectName}_MCP (TOOL PROVIDER)                          │
│                                                                          │
│   We expose:                                                            │
│   • MCP tools (via Node.js bridge)                                      │
│   • Project context (Context/*.json)                                    │
│   • AI memory (Memory/*.json)                                           │
│   • Test scenarios (Tester/*.json)                                      │
│                                                                          │
│   We do NOT:                                                            │
│   • Make AI API calls                                                   │
│   • Store API keys                                                      │
│   • Run inference                                                       │
│                                                                          │
│   The AI in VS Code/Cline reads our context and uses our tools.        │
└──────────────────────────────────┬──────────────────────────────────────┘
                                   │
                              HTTP :9847
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     AgenticMCP (C++ Plugin)                              │
│                   Does the actual UE manipulation                       │
└─────────────────────────────────────────────────────────────────────────┘
```

| Component | Role | Has AI? |
|-----------|------|---------|
| **VS Code / Cline / Claude Desktop** | AI Host | ✅ Yes (their API keys) |
| **{ProjectName}_MCP** | Tool Provider + Context | ❌ No |
| **AgenticMCP** | UE Manipulation Engine | ❌ No |

---

## Auto-Context Injection on Connect

When the AI first connects, it should **immediately receive all project context** without having to ask.

### How Standard MCP Works (NOT what we want)

```
1. AI connects
2. Server: "Here are my tools: [list of 50 tools]"
3. AI: "What's the project context?"         ← Extra step
4. Server: "Here's the context..."
5. AI can now work
```

### What We Do Instead (Auto-Injection)

```
1. AI connects
2. Server: "Here are my tools: [list]
            AND here's your context:
            - Project: MyGame
            - Current level: Level_01
            - Memory: [past decisions]
            - Abilities: [what player can do]
            - Custom endpoints: [project-specific]
            - Test scenarios: [available tests]"
3. AI can IMMEDIATELY work with full knowledge
```

### Implementation: MCP System Prompt Injection

The Node.js bridge injects context into the **tools/list response** or uses MCP's **prompts** feature:

```javascript
// Tools/index.js

// On initialization, load all context
const projectContext = {
  project: loadJson('Context/config.json'),
  levels: loadJson('Context/levels.json'),
  actors: loadJson('Context/actors.json'),
  memory: {
    decisions: loadJson('Memory/decisions.json'),
    patterns: loadJson('Memory/patterns.json'),
    lastSession: getLatestSession('Memory/sessions/')
  },
  tester: {
    abilities: loadJson('Tester/abilities.json'),
    gameRules: loadJson('Tester/game_rules.json'),
    exposedData: loadJson('Tester/exposed_data.json')
  },
  customEndpoints: loadJson('Context/custom_endpoints.json')
};

// MCP prompts feature - auto-loaded by compatible clients
const prompts = [
  {
    name: "project_context",
    description: "Full project context - auto-injected on connect",
    arguments: [],
    messages: [
      {
        role: "system",
        content: formatContextForAI(projectContext)
      }
    ]
  }
];

// Alternative: Include in tool descriptions
const tools = [
  {
    name: "get_full_context",
    description: `
      Returns full project context. CALL THIS FIRST.

      Quick summary (always available):
      - Project: ${projectContext.project.projectName}
      - Engine: ${projectContext.project.engineVersion}
      - Levels: ${projectContext.levels.length}
      - Past decisions: ${projectContext.memory.decisions?.length || 0}
      - Known patterns: ${projectContext.memory.patterns?.length || 0}
      - Test scenarios: available

      Call this tool to get the complete context.
    `,
    inputSchema: { type: "object", properties: {} }
  },
  // ... other tools
];
```

### Context Format for AI

```markdown
# Project Context: MyGame

## Project Info
- **Name:** MyGame
- **Engine:** UE 5.6
- **AgenticMCP:** local (http://localhost:9847)

## Current State
- **Loaded Level:** Level_01
- **Actor Count:** 1,523
- **PIE Status:** stopped

## Memory (Past Sessions)
### Recent Decisions
1. [2026-03-11] Added enemy pooling pattern - 10x spawn speedup
2. [2026-03-10] Created preload system for Scene 9 assets

### Known Patterns
- `actor_pooling` - Reuse spawned actors instead of create/destroy
- `level_preload` - Start loading next level before transition

## Player Abilities (for testing)
- move, jump, sprint, crouch
- attack, block, dodge
- interact, inventory, pause

## Custom Endpoints
- `/api/project/player-state` - Get health, mana, inventory
- `/api/project/quest-status` - Get active quests
- `/api/project/enemy-count` - Count active enemies

## Available Test Scenarios
- `happy_path` - Normal playthrough
- `edge_cases` - Boundary conditions
- `stress_test` - Performance limits
- `break_attempts` - Try to break the game

## Rules
- Verify with screenshot before AND after actions
- One action at a time, verify, then proceed
- Log all decisions to Memory/
```

### MCP Clients That Support This

| Client | Auto-Context Method |
|--------|---------------------|
| **Claude Desktop** | Uses `prompts` feature, auto-loads on connect |
| **Cline (VS Code)** | Reads tool descriptions, can call `get_full_context` |
| **Cursor** | Similar to Cline |
| **Custom Tool** | Can implement any method |

### Fallback: CLAUDE.md / .cursorrules

For clients that don't support MCP prompts, we also generate:

```
{ProjectName}_MCP/
├── CLAUDE.md           ← Claude Desktop reads this automatically
├── .cursorrules        ← Cursor reads this automatically
└── AI_CONTEXT.md       ← Generic, for any AI to read
```

These files contain the same context and are regenerated whenever context changes.

---

## Quantized Context (Token Optimization)

To reduce token usage, we send a **compressed context format** instead of verbose prose.

### Verbose vs Quantized

**Verbose (wasteful):**
```markdown
# Project Context: MyGame

## Project Information
- **Project Name:** MyGame
- **Unreal Engine Version:** 5.6
- **AgenticMCP Location:** Local plugin at http://localhost:9847
- **Current Status:** Editor is running

## Currently Loaded Level
- **Level Name:** Level_01
- **Full Path:** /Game/Maps/Level_01
- **Number of Actors:** 1,523
- **Play-In-Editor Status:** Not running

## Memory from Past Sessions
### Recent Decisions Made by AI
1. On 2026-03-11, I added an enemy pooling pattern which resulted in 10x spawn speedup
2. On 2026-03-10, I created a preload system for Scene 9 assets to reduce hitching

... (500+ tokens)
```

**Quantized (efficient):**
```
CTX|MyGame|UE5.6|local:9847|up
LVL|Level_01|/Game/Maps/Level_01|1523|pie:off
MEM|d:2|p:2|s:3
D|2026-03-11|actor_pool|10x spawn|OK
D|2026-03-10|preload_s9|no hitch|OK
P|actor_pooling|spawn>10/s
P|level_preload|transition>1s
ABL|move,jump,sprint,attack,block,interact
END|/project/player-state,/project/quest-status,/project/enemy-count
TST|happy_path,edge_cases,stress_test,break_attempts
```
**(~80 tokens vs 500+)**

### Quantized Format Specification

```
HEADER
CTX|{project}|{engine}|{mcp_location}|{status}

LEVEL
LVL|{name}|{path}|{actors}|pie:{on/off}

MEMORY SUMMARY
MEM|d:{decisions}|p:{patterns}|s:{sessions}

DECISIONS (recent 5)
D|{date}|{name}|{outcome}|{status}

PATTERNS
P|{name}|{trigger}

ABILITIES (comma list)
ABL|{ability1},{ability2},...

CUSTOM ENDPOINTS (paths only)
END|{path1},{path2},...

TEST SCENARIOS (names only)
TST|{scenario1},{scenario2},...

RULES (abbreviated)
RUL|ss:before+after|1act|verify|log
```

### Key Abbreviations

| Abbrev | Meaning |
|--------|---------|
| `CTX` | Context header |
| `LVL` | Level info |
| `MEM` | Memory summary |
| `D` | Decision |
| `P` | Pattern |
| `ABL` | Abilities |
| `END` | Endpoints |
| `TST` | Test scenarios |
| `RUL` | Rules |
| `ss` | Screenshot |
| `pie` | Play-In-Editor |
| `OK` | Success |
| `FAIL` | Failure |

### Implementation

```javascript
// Tools/context-quantizer.js

function quantizeContext(context) {
  const lines = [];

  // Header
  lines.push(`CTX|${context.project.name}|UE${context.project.engine}|${context.mcp.mode}:${context.mcp.port}|${context.status}`);

  // Level
  if (context.level) {
    lines.push(`LVL|${context.level.name}|${context.level.path}|${context.level.actors}|pie:${context.pie ? 'on' : 'off'}`);
  }

  // Memory summary
  const mem = context.memory;
  lines.push(`MEM|d:${mem.decisions?.length || 0}|p:${mem.patterns?.length || 0}|s:${mem.sessions?.length || 0}`);

  // Recent decisions (last 5)
  mem.decisions?.slice(-5).forEach(d => {
    lines.push(`D|${d.date}|${d.name}|${d.outcome}|${d.status}`);
  });

  // Patterns
  mem.patterns?.forEach(p => {
    lines.push(`P|${p.name}|${p.trigger}`);
  });

  // Abilities (comma-separated)
  if (context.abilities?.length) {
    lines.push(`ABL|${context.abilities.join(',')}`);
  }

  // Endpoints (paths only)
  if (context.endpoints?.length) {
    lines.push(`END|${context.endpoints.map(e => e.path).join(',')}`);
  }

  // Test scenarios
  if (context.tests?.length) {
    lines.push(`TST|${context.tests.join(',')}`);
  }

  // Rules (always same)
  lines.push(`RUL|ss:before+after|1act|verify|log`);

  return lines.join('\n');
}
```

### Expansion on AI Side

The AI knows to expand:
- `CTX|MyGame|UE5.6|local:9847|up` → "Project MyGame, Unreal 5.6, local MCP on port 9847, editor running"
- `D|2026-03-11|actor_pool|10x spawn|OK` → "On 2026-03-11, implemented actor_pool pattern, achieved 10x spawn improvement, successful"
- `RUL|ss:before+after|1act|verify|log` → "Screenshot before and after, one action at a time, verify each action, log to memory"

### Token Savings

| Context Type | Verbose | Quantized | Savings |
|--------------|---------|-----------|---------|
| Project info | ~50 | ~15 | 70% |
| Level info | ~40 | ~12 | 70% |
| 5 decisions | ~200 | ~50 | 75% |
| 3 patterns | ~100 | ~25 | 75% |
| Abilities | ~30 | ~10 | 67% |
| Endpoints | ~50 | ~15 | 70% |
| **Total** | ~500+ | ~130 | **~75%** |

### When to Use Verbose vs Quantized

| Scenario | Format |
|----------|--------|
| First connection (AI needs to learn format) | Verbose (once) |
| Subsequent messages | Quantized |
| AI requests full details | Verbose on-demand |
| Tight token budget | Quantized always |

### Self-Describing Header

Include format version so AI knows how to parse:

```
QCTX:v1
CTX|MyGame|UE5.6|local:9847|up
...
```

AI sees `QCTX:v1` and knows to use quantized parsing rules.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         AI AGENT / USER                                  │
│                    (Claude, Cursor, Manus, etc.)                        │
└──────────────────────────────────┬──────────────────────────────────────┘
                                   │
                          MCP Protocol (stdio)
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     {ProjectName}_MCP                                    │
│              Lives in: Project/Plugins/{ProjectName}_MCP/               │
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  Tools/ (Node.js MCP Bridge)                                     │   │
│   │  • Entry point for MCP protocol                                  │   │
│   │  • Routes commands to AgenticMCP                                 │   │
│   │  • AI CAN MODIFY THIS (live adaptation)                         │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  Context/ (Project Knowledge)                                    │   │
│   │  • config.json - AgenticMCP location, settings                  │   │
│   │  • levels.json - Auto-scanned level data                        │   │
│   │  • actors.json - Actor catalog                                  │   │
│   │  • custom_endpoints.json - Project-specific endpoints           │   │
│   │  • AI CAN MODIFY ALL OF THIS                                    │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  Memory/ (AI Learning Persistence) ← THE KEY                    │   │
│   │  • decisions.json - AI decisions & rationale                    │   │
│   │  • patterns.json - Discovered patterns & shortcuts              │   │
│   │  • history.json - Changelog with rollback points                │   │
│   │  • session_*.json - Per-session learnings                       │   │
│   │  • PERSISTS ACROSS SESSIONS                                     │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└──────────────────────────────────┬──────────────────────────────────────┘
                                   │
                              HTTP :9847
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │                          │                          │
        ▼                          ▼                          ▼
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│  AgenticMCP      │    │  AgenticMCP      │    │  AgenticMCP      │
│  (Local Plugin)  │    │  (Marketplace)   │    │  (Network)       │
└──────────────────┘    └──────────────────┘    └──────────────────┘
```

---

## AI Learning & Propagation

### The Problem
AI makes decisions, solves problems, discovers shortcuts. Next session? Starts from zero.

### The Solution
`{ProjectName}_MCP/Memory/` - AI writes its learnings here. Persists. Compounds.

### Memory Structure

```
{ProjectName}_MCP/
└── Memory/
    ├── decisions.json          ← What AI decided and why
    ├── patterns.json           ← Discovered patterns/shortcuts
    ├── history.json            ← Full changelog with rollback
    ├── sessions/
    │   ├── 2026-03-11_001.json ← Session 1 learnings
    │   ├── 2026-03-11_002.json ← Session 2 learnings
    │   └── ...
    └── snapshots/
        ├── pre_change_001.json ← State before risky change
        └── ...
```

### decisions.json

```json
{
  "decisions": [
    {
      "id": "d001",
      "timestamp": "2026-03-11T19:30:00Z",
      "context": "User asked to optimize Scene 9 loading",
      "decision": "Created custom endpoint /api/project/preload-scene9-assets",
      "rationale": "Scene 9 has 200MB of textures that cause hitching. Preloading on Scene 8 exit eliminates this.",
      "files_modified": [
        "Context/custom_endpoints.json",
        "Tools/handlers/scene9_preload.js"
      ],
      "outcome": "success",
      "reusable": true
    },
    {
      "id": "d002",
      "timestamp": "2026-03-11T19:45:00Z",
      "context": "Spawning enemies was slow",
      "decision": "Added actor pool pattern to spawn-enemy endpoint",
      "rationale": "Instead of spawning fresh, reuse pooled actors. 10x faster.",
      "code_pattern": "actor_pooling",
      "reusable": true
    }
  ]
}
```

### patterns.json

```json
{
  "patterns": [
    {
      "name": "actor_pooling",
      "description": "Pre-spawn actors and reuse instead of create/destroy",
      "trigger": "Frequent spawn/destroy of same actor type",
      "implementation": "See handlers/actor_pool.js",
      "learned_from": "d002",
      "times_used": 5
    },
    {
      "name": "level_preload",
      "description": "Start loading next level's assets before transition",
      "trigger": "Level transitions cause hitching",
      "implementation": "See handlers/preloader.js",
      "learned_from": "d001",
      "times_used": 3
    }
  ]
}
```

### history.json (Changelog with Rollback)

```json
{
  "history": [
    {
      "version": 15,
      "timestamp": "2026-03-11T19:50:00Z",
      "action": "Added custom endpoint: /api/project/spawn-enemy",
      "files": ["Context/custom_endpoints.json"],
      "snapshot": "snapshots/pre_change_015.json",
      "can_rollback": true
    },
    {
      "version": 14,
      "timestamp": "2026-03-11T19:30:00Z",
      "action": "Updated levels.json with Scene 9 actors",
      "files": ["Context/levels.json"],
      "snapshot": "snapshots/pre_change_014.json",
      "can_rollback": true
    }
  ],
  "current_version": 15
}
```

---

## AI Live Coding Flow

```
┌────────────────────────────────────────────────────────────────────────┐
│                           AI SESSION                                    │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  1. LOAD MEMORY                                                         │
│                                                                          │
│  AI reads:                                                              │
│  • Memory/decisions.json → Past decisions and outcomes                 │
│  • Memory/patterns.json → Known patterns to apply                      │
│  • Memory/sessions/latest.json → Last session context                  │
│  • Context/*.json → Current project state                              │
│                                                                          │
│  AI now has: Full history + learned patterns + current state           │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  2. WORK                                                                │
│                                                                          │
│  AI encounters problem → Checks if pattern exists in Memory/           │
│  • Pattern exists? Apply it.                                           │
│  • New problem? Solve it, then persist the solution.                   │
│                                                                          │
│  AI makes real-time modifications to:                                  │
│  • Context/custom_endpoints.json (add/modify endpoints)                │
│  • Tools/handlers/*.js (add handler code)                              │
│  • Context/*.json (update project knowledge)                           │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  3. PERSIST                                                             │
│                                                                          │
│  Before modifying any file:                                            │
│  • Snapshot current state → Memory/snapshots/                          │
│  • Log change → Memory/history.json                                    │
│                                                                          │
│  After solving a problem:                                              │
│  • Record decision → Memory/decisions.json                             │
│  • Extract pattern → Memory/patterns.json (if reusable)                │
│  • Update session → Memory/sessions/{date}_{n}.json                    │
│                                                                          │
│  EVERYTHING IS PERSISTED. NOTHING IS LOST.                             │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  4. NEXT SESSION                                                        │
│                                                                          │
│  New session starts → AI loads Memory/ → Has all previous learnings   │
│  • Knows past decisions and why                                        │
│  • Knows patterns that work                                            │
│  • Can rollback if needed                                              │
│                                                                          │
│  AI IS NOW SMARTER THAN LAST SESSION                                   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## File Structure (Complete)

```
YourProject/
├── Plugins/
│   ├── AgenticMCP/                      ← Core (C++, from Git/Marketplace)
│   │   ├── AgenticMCP.uplugin
│   │   ├── Source/AgenticMCP/           ← All UE manipulation code
│   │   └── README.md
│   │
│   └── {ProjectName}_MCP/               ← Project-specific (NO C++)
│       ├── {ProjectName}_MCP.uplugin    ← Content-only plugin manifest
│       │
│       ├── Tools/                       ← Node.js MCP Bridge
│       │   ├── index.js                 ← MCP entry point
│       │   ├── lib.js                   ← HTTP client
│       │   ├── router.js                ← Routes to AgenticMCP
│       │   ├── memory.js                ← Memory read/write
│       │   ├── package.json
│       │   └── handlers/                ← Custom endpoint handlers
│       │       ├── scene_preload.js     ← AI-created
│       │       ├── actor_pool.js        ← AI-created
│       │       └── ...
│       │
│       ├── Context/                     ← Project knowledge
│       │   ├── config.json              ← AgenticMCP location, settings
│       │   ├── levels.json              ← Auto-scanned level data
│       │   ├── actors.json              ← Actor catalog
│       │   ├── blueprints.json          ← Blueprint catalog
│       │   └── custom_endpoints.json    ← Custom endpoint definitions
│       │
│       ├── Memory/                      ← AI LEARNING PERSISTENCE
│       │   ├── decisions.json           ← AI decisions + rationale
│       │   ├── patterns.json            ← Discovered patterns
│       │   ├── history.json             ← Changelog + rollback
│       │   ├── sessions/                ← Per-session learnings
│       │   │   ├── 2026-03-11_001.json
│       │   │   └── ...
│       │   └── snapshots/               ← Pre-change snapshots
│       │       ├── pre_change_001.json
│       │       └── ...
│       │
│       └── Tester/                      ← AI-CODED SIMULATED TESTER
│           ├── abilities.json           ← What player can do (jump, shoot, etc.)
│           ├── game_rules.json          ← Win/lose conditions, progression
│           ├── exposed_data.json        ← Custom project data endpoints
│           ├── scenarios/               ← Test scenarios (AI-generated)
│           │   ├── happy_path.json      ← Normal playthrough
│           │   ├── edge_cases.json      ← Boundary conditions
│           │   ├── stress_test.json     ← Performance/load testing
│           │   └── break_attempts.json  ← Try to break the game
│           ├── scripts/                 ← Test driver scripts
│           │   ├── player_sim.js        ← Simulates player behavior
│           │   ├── input_sequences.js   ← Pre-recorded input patterns
│           │   └── verifier.js          ← State verification
│           └── results/                 ← Test run outputs
│               ├── run_001/
│               │   ├── report.json
│               │   ├── screenshots/
│               │   └── failures/
│               └── ...
│
└── YourProject.uproject
```

---

## The .uplugin (Content-Only, No C++)

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "1.0.0",
  "FriendlyName": "MyGame MCP",
  "Description": "Project-specific MCP context and AI memory",
  "Category": "Editor",
  "CreatedBy": "AgenticMCP",
  "EnabledByDefault": true,
  "CanContainContent": true,
  "Plugins": [
    {
      "Name": "AgenticMCP",
      "Enabled": true
    }
  ]
}
```

**No "Modules" = No C++ = AI can freely modify**

---

## Setup

### 1. Install AgenticMCP

```bash
# Option A: Git clone to project
cd YourProject/Plugins/
git clone https://github.com/AniketMan/agentic-mcp-server.git AgenticMCP

# Option B: Marketplace (coming soon)
# Install from Epic Games Launcher
```

### 2. Initialize Project Plugin

```bash
cd YourProject/Plugins/AgenticMCP/Tools/
npm install
npm run init
# Creates: YourProject/Plugins/YourProject_MCP/
```

### 3. Configure MCP Client

```json
{
  "mcpServers": {
    "unreal": {
      "command": "node",
      "args": ["C:/Path/To/YourProject/Plugins/YourProject_MCP/Tools/index.js"]
    }
  }
}
```

### 4. Open Unreal Editor

AgenticMCP starts → Scans project → Updates `{ProjectName}_MCP/Context/`

---

## AI Rules for Memory

When working in this project, AI should:

### On Session Start
```javascript
// Load existing memory
const decisions = await readJson('Memory/decisions.json');
const patterns = await readJson('Memory/patterns.json');
const lastSession = await getLatestSession('Memory/sessions/');

// AI now has full context
```

### Before Any Modification
```javascript
// 1. Snapshot current state
await snapshot(file, 'Memory/snapshots/pre_change_XXX.json');

// 2. Log to history
await appendHistory({
  action: "What I'm about to do",
  files: [file],
  snapshot: 'pre_change_XXX.json'
});
```

### After Solving a Problem
```javascript
// 1. Record the decision
await appendDecision({
  context: "What was the problem",
  decision: "What I did",
  rationale: "Why I did it",
  files_modified: [...],
  outcome: "success|failure",
  reusable: true|false
});

// 2. Extract pattern if reusable
if (reusable) {
  await appendPattern({
    name: "pattern_name",
    description: "...",
    trigger: "When to use this",
    implementation: "Where the code is"
  });
}
```

### On Rollback Request
```javascript
// Find snapshot in history
const snapshot = history.find(h => h.version === targetVersion);

// Restore from snapshot
await restore(snapshot.files, snapshot.snapshot);

// Log the rollback
await appendHistory({
  action: "Rolled back to version " + targetVersion,
  reason: "Why"
});
```

---

## Network Mode: Multi-Engine

One `{ProjectName}_MCP` can control multiple AgenticMCP instances:

```json
// Context/config.json
{
  "agenticMcp": {
    "mode": "network",
    "engines": [
      {"id": "local", "url": "http://localhost:9847"},
      {"id": "render-1", "url": "http://192.168.1.10:9847"},
      {"id": "render-2", "url": "http://192.168.1.11:9847"}
    ]
  }
}
```

Commands:
- `/mcp/engine/local/api/screenshot` → Local engine
- `/mcp/engine/render-1/api/start-lightmass` → Render node 1
- `/mcp/broadcast/api/save-all` → All engines

---

## Summary

| What | Where | Who Modifies | Persists |
|------|-------|--------------|----------|
| UE manipulation code | AgenticMCP (C++) | Developer | Yes (git) |
| Project context | {ProjectName}_MCP/Context/ | AI + AgenticMCP | Yes (files) |
| AI decisions | {ProjectName}_MCP/Memory/ | AI | Yes (files) |
| AI patterns | {ProjectName}_MCP/Memory/ | AI | Yes (files) |
| Custom endpoints | {ProjectName}_MCP/Context/ + Tools/handlers/ | AI + Developer | Yes (files) |
| MCP Bridge | {ProjectName}_MCP/Tools/ | AI + Developer | Yes (files) |

**The AI gets smarter every session. Nothing is lost.**

---

## Doc-to-Game Pipeline

The ultimate capability: Give AI a design document, script, or live prompts → Get a fully built, tested game.

### Input Types

| Input | Example |
|-------|---------|
| **Game Design Doc** | PDF/MD describing levels, mechanics, story |
| **Script/Screenplay** | Dialogue, scene descriptions, stage directions |
| **Live Prompting** | "Add a boss fight to level 3" |
| **Reference Material** | Concept art, mood boards, audio samples |

### Build Phase

```
┌─────────────────────────────────────────────────────────────────────────┐
│  AI READS DESIGN DOC                                                     │
│                                                                          │
│  Extracts:                                                              │
│  • Levels to create                                                     │
│  • Actors to spawn                                                      │
│  • Logic to implement                                                   │
│  • Audio to place                                                       │
│  • Sequences to build                                                   │
│                                                                          │
│  Creates task list → Stores in Memory/tasks.json                        │
└─────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  AI EXECUTES TASKS VIA AgenticMCP                                        │
│                                                                          │
│  For each task:                                                         │
│  1. Execute via MCP endpoint                                            │
│  2. Verify via screenshot/state check                                   │
│  3. Log decision + outcome to Memory/                                   │
│  4. If failure → retry with different approach                          │
│  5. Update task status                                                  │
│                                                                          │
│  All progress persisted. Can resume from any point.                    │
└─────────────────────────────────────────────────────────────────────────┘
```

### Test Phase (AI Self-Verification)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  AI TESTS ITS OWN WORK                                                   │
│                                                                          │
│  /api/start-pie                    → Run the game                       │
│  /api/screenshot                   → Capture current state              │
│  /api/simulate-input               → Play through as user               │
│  /api/get-pie-state                → Check game state                   │
│  /api/execute-python               → Query runtime values               │
│                                                                          │
│  Compare screenshots to expected states                                 │
│  Check if story triggers fire                                           │
│  Verify sequences play correctly                                        │
│  Confirm audio cues work                                                │
│                                                                          │
│  Log all results to Memory/tests/                                       │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Simulated Tester: Multi-Modal Awareness

The AI codes a **Simulated Tester** into `{ProjectName}_MCP/Tester/` that can run through the game as a player or rigorous QA tester. It has access to **three data sources**:

### Data Source 1: Visual (Screenshots)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  /api/screenshot → Base64 JPEG                                          │
│                                                                          │
│  The tester can SEE:                                                    │
│  • What the player sees                                                 │
│  • UI elements (health bars, menus)                                    │
│  • Visual bugs (z-fighting, pop-in, incorrect textures)               │
│  • Screen state matches expectations                                   │
│                                                                          │
│  Can compare screenshots to:                                            │
│  • Reference images (stored in Tester/expected/)                       │
│  • Previous screenshots (regression testing)                           │
│  • Perceptual hash (detect visual changes)                             │
└─────────────────────────────────────────────────────────────────────────┘
```

### Data Source 2: Engine Data (AgenticMCP Endpoints)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Everything AgenticMCP exposes:                                          │
│                                                                          │
│  /api/list-actors        → All actors, transforms, properties          │
│  /api/get-actor          → Detailed actor info                         │
│  /api/get-pie-state      → Is running, time elapsed, FPS               │
│  /api/get-camera         → Camera position/rotation                    │
│  /api/audio/active-sounds → What sounds are playing                    │
│  /api/niagara/stats      → Particle system performance                 │
│  /api/read-sequence      → Level sequence state                        │
│  /api/get-cvar           → Console variable values                     │
│  /api/execute-python     → Query ANY runtime data                      │
│                                                                          │
│  This is EVERYTHING the engine knows.                                   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Data Source 3: Project Data (Custom Exposed)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Developer exposes game-specific data via {ProjectName}_MCP:            │
│                                                                          │
│  /api/project/player-state    → Health, mana, stamina, inventory       │
│  /api/project/quest-status    → Active quests, objectives, completion  │
│  /api/project/game-progress   → Current chapter, unlocks, checkpoints  │
│  /api/project/dialogue-state  → Current conversation, choices made     │
│  /api/project/enemy-count     → Active enemies, spawned, killed        │
│  /api/project/score           → Points, combos, multipliers           │
│  /api/project/save-state      → Current save slot data                 │
│                                                                          │
│  Defined in: Tester/exposed_data.json                                   │
│  Registered via: Context/custom_endpoints.json                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### exposed_data.json Example

```json
{
  "exposedData": [
    {
      "name": "player-state",
      "path": "/api/project/player-state",
      "pythonQuery": "get_player_state()",
      "returns": {
        "health": "float",
        "maxHealth": "float",
        "mana": "float",
        "inventory": "array"
      }
    },
    {
      "name": "quest-status",
      "path": "/api/project/quest-status",
      "pythonQuery": "get_quest_manager().get_active_quests()",
      "returns": {
        "activeQuests": "array",
        "completedQuests": "array"
      }
    },
    {
      "name": "enemy-count",
      "path": "/api/project/enemy-count",
      "actorClassFilter": "BP_Enemy",
      "returns": {
        "count": "int",
        "enemies": "array"
      }
    }
  ]
}
```

### Tester Decision Making

The AI combines ALL data sources for verification:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  EXAMPLE: Verify "Taking damage reduces health"                         │
│                                                                          │
│  BEFORE:                                                                │
│  • Screenshot shows health bar at ~100%                                 │
│  • /api/project/player-state returns { health: 100, maxHealth: 100 }   │
│  • /api/list-actors shows BP_Enemy_001 at (100, 200, 0)               │
│                                                                          │
│  ACTION:                                                                │
│  • /api/simulate-input → Move player into enemy                        │
│  • Wait 500ms                                                          │
│                                                                          │
│  AFTER:                                                                 │
│  • Screenshot shows health bar at ~80%                                  │
│  • /api/project/player-state returns { health: 80, maxHealth: 100 }    │
│  • Visual matches data? ✓                                              │
│  • Health reduced by expected amount? ✓                                │
│                                                                          │
│  RESULT: PASS                                                           │
│  Log to: Tester/results/run_001/damage_test.json                       │
└─────────────────────────────────────────────────────────────────────────┘
```

### Test Modes

| Mode | Behavior |
|------|----------|
| **Happy Path** | Play through as normal player would. Follow intended flow. |
| **Rigorous QA** | Test every interaction. Edge cases. Boundary conditions. |
| **Stress Test** | Spawn many actors. Rapid inputs. Performance limits. |
| **Break Attempts** | Try to sequence break. Exploit physics. Clip through walls. |
| **Regression** | Run previous scenarios. Compare to known-good results. |

### scenarios/break_attempts.json Example

```json
{
  "scenario": "break_attempts",
  "description": "Try to break the game through unusual behavior",
  "tests": [
    {
      "name": "clip_through_wall",
      "description": "Try to clip through walls using sprint+jump",
      "steps": [
        { "action": "move_to", "target": "wall_corner_001" },
        { "action": "input", "keys": ["sprint", "jump"], "duration": 2000 },
        { "action": "screenshot" }
      ],
      "verify": {
        "player_position": "should be on valid navmesh",
        "should_not": "be inside wall geometry"
      }
    },
    {
      "name": "rapid_pause_unpause",
      "description": "Rapidly pause/unpause during critical animation",
      "steps": [
        { "action": "trigger", "event": "start_cutscene" },
        { "action": "loop", "count": 20, "do": [
          { "action": "input", "keys": ["pause"] },
          { "action": "wait", "ms": 50 },
          { "action": "input", "keys": ["pause"] },
          { "action": "wait", "ms": 50 }
        ]},
        { "action": "screenshot" }
      ],
      "verify": {
        "game_state": "should not be corrupted",
        "animation": "should complete normally"
      }
    },
    {
      "name": "inventory_overflow",
      "description": "Try to exceed inventory limits",
      "steps": [
        { "action": "python", "code": "spawn_pickup_items(999)" },
        { "action": "input", "keys": ["interact"], "repeat": 100 }
      ],
      "verify": {
        "inventory_count": "should not exceed max",
        "no_crash": true
      }
    }
  ]
}
```

### Test Report Structure

```json
// Memory/tests/2026-03-11_playthrough.json
{
  "testRun": "2026-03-11T19:30:00Z",
  "type": "full_playthrough",
  "scenes": [
    {
      "scene": "Level_01",
      "tests": [
        {
          "name": "Player spawns correctly",
          "method": "screenshot_compare",
          "expected": "player visible at spawn point",
          "result": "pass",
          "screenshot": "screenshots/level01_spawn.jpg"
        },
        {
          "name": "Door opens on trigger",
          "method": "simulate_input + state_check",
          "expected": "door actor rotation changes",
          "result": "pass"
        },
        {
          "name": "Boss appears in phase 2",
          "method": "actor_list_check",
          "expected": "BP_Boss in level",
          "result": "fail",
          "error": "BP_Boss not spawned",
          "fix_attempted": true,
          "fix_outcome": "Added spawn trigger, retested, now passes"
        }
      ]
    }
  ],
  "summary": {
    "total": 45,
    "passed": 44,
    "failed": 1,
    "fixed": 1
  }
}
```

### Resume from Anywhere

Because everything is in Memory/:
- **Session interrupted?** Resume from last completed task
- **Want to rebuild?** Replay decisions.json from scratch
- **New requirement?** Add to tasks, continue from current state
- **Something broke?** Rollback to snapshot, try different approach

### Example: Script to Game

```
INPUT: screenplay.md
├── Scene 1: Kitchen - Morning
│   "SARAH enters, looks tired. She pours coffee."
│
├── Scene 2: Living Room - Morning
│   "SARAH sits on couch. Phone RINGS."
│
└── Scene 3: Front Door - Morning
    "SARAH opens door. DETECTIVE stands there."

                    ↓ AI processes ↓

OUTPUT: Built levels + sequences
├── Level_Kitchen
│   ├── BP_Sarah spawned at entrance
│   ├── Sequence_Sarah_EnterKitchen
│   ├── Coffee machine interaction trigger
│   └── Audio: footsteps, coffee pour
│
├── Level_LivingRoom
│   ├── BP_Sarah spawned at doorway
│   ├── Couch interaction point
│   ├── Phone actor with ring trigger
│   └── Audio: phone ring, ambient
│
└── Level_FrontDoor
    ├── BP_Sarah at door
    ├── BP_Detective at porch
    ├── Door open animation sequence
    └── Audio: door creak, dialogue cues
```

### The Memory Enables This

Without Memory/:
- AI forgets what it built
- Can't resume interrupted sessions
- Can't learn from failed approaches
- Tests aren't recorded

With Memory/:
- Full build history
- Resumable from any point
- Patterns extracted from successes
- Complete test records
- **The project is reproducible**

---

## Implementation Checklist

### Phase 1: Foundation
- [ ] Create `{ProjectName}_MCP` folder structure
- [ ] Implement Tools/index.js (MCP bridge)
- [ ] Implement Tools/memory.js (persistence layer)
- [ ] Implement Context/ scanning
- [ ] Create init script (`npm run init`)

### Phase 2: Memory System
- [ ] decisions.json read/write
- [ ] patterns.json read/write
- [ ] history.json with snapshots
- [ ] Session persistence
- [ ] Rollback capability

### Phase 3: AI Integration
- [ ] Load memory on session start
- [ ] Persist decisions on every action
- [ ] Extract patterns on success
- [ ] Test framework integration

### Phase 4: Doc-to-Game
- [ ] Design doc parser
- [ ] Task list generator
- [ ] Progress tracker
- [ ] Self-verification system
- [ ] Test report generator

---

## License

MIT
