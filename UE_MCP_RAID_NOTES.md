# UE-MCP Repo Raid Notes

## Architecture

UE-MCP is a TypeScript MCP server + Python WebSocket bridge that runs INSIDE Unreal Editor.

### Two Modes
1. **Live Mode**: MCP server (Node.js) connects via WebSocket to a Python bridge running inside UE editor. All operations go through `import unreal` on the game thread.
2. **Offline Mode**: Only `asset.list` works offline (scans filesystem). Everything else requires the live editor connection.

### Key Insight: They CANNOT Write Offline
- Their `add_node()` and `connect_pins()` are **stubs** that return fake success messages with notes like "Node placement via Python is limited"
- Blueprint graph manipulation (add_node, connect_pins, delete_node) all require the live editor
- They have NO UAssetAPI integration — zero binary editing capability
- Their "offline" is just filesystem scanning

## What We Have That They Don't
1. **Direct binary editing via UAssetAPI** — we can read AND write .uasset/.umap without the editor
2. **Byte-perfect round-trips** on UE 5.6 Oculus branch assets
3. **MetaData section fix** — we patched a UAssetAPI bug they would also hit
4. **PropertyTypeName support** — UE 5.4+ property serialization that UAssetAPI's published DLL didn't handle

## What They Have That's Worth Stealing

### 1. Auto-Deploy Pattern (deployer.ts)
- Automatically enables PythonScriptPlugin in .uproject
- Copies Python bridge files to Content/Python/
- Configures startup script in DefaultEngine.ini
- Installs websockets in UE's Python environment
- **Useful for**: Our JarvisEditor plugin auto-deployment

### 2. Project Context (project.ts)
- Clean `/Game/` path resolution
- Content directory discovery from .uproject
- Engine association parsing
- **Useful for**: Our API should accept game paths, not just filesystem paths

### 3. Category Tool Pattern (types.ts)
- Groups 220+ actions into 18 mega-tools
- Each tool has an `action` parameter that selects the operation
- Clean schema definition with Zod
- **Useful for**: If we ever wrap our API as an MCP server

### 4. Game Thread Dispatch (bridge_server.py)
- WebSocket server running in a daemon thread
- Queues handler calls to the game thread via Slate tick callback
- `unreal.register_slate_post_tick_callback()` for safe game-thread execution
- **Useful for**: Our JarvisEditor C++ plugin's local-AI workflow

### 5. Handler Coverage
They have handlers for:
- Asset management (CRUD, import, datatables, textures)
- Blueprint (read, create, variables, functions, components)
- Level (outliner, place/delete/move actors, volumes, lights, splines)
- Material (read, create, parameters, graph authoring)
- Animation (montages, blendspaces, skeletons)
- PCG (graphs, nodes, execution)
- Niagara (systems, emitters, modules)
- Sequencer (sequences, tracks)
- Editor (console, Python exec, PIE, viewport, build pipeline)
- Reflection (class/struct/enum inspection)
- Gameplay (physics, collision, navigation, input, behavior trees, AI)
- GAS (Gameplay Ability System)
- Networking (replication)

### 6. Reflection Handler Pattern
- `reflect_class`, `reflect_struct`, `reflect_enum` for runtime type inspection
- **Useful for**: Understanding what properties an actor class expects before we add them

## What's NOT Worth Stealing
- Their WebSocket bridge protocol (we use REST, simpler)
- Their Node.js MCP wrapper (we'd build our own if needed)
- Their `add_node`/`connect_pins` stubs (they don't actually work)
- Their Windows-only engine discovery (registry-based)

## Actionable Items
1. **Add `/Game/` path resolution** to our API
2. **Steal the auto-deploy pattern** for the JarvisEditor plugin
3. **Consider MCP server wrapper** for our Level Editor API (future)
4. **Use their handler list** as a feature checklist for our write operations
