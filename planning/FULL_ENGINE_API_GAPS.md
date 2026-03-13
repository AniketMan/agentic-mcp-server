# AgenticMCP: Full Engine API Gap Analysis

**Current:** 107 endpoints across 24 categories
**Goal:** Expose every meaningful UE5 editor operation an AI agent could need

---

## ALREADY COVERED (24 categories, 107 endpoints)

1. Health/Lifecycle (2)
2. Blueprint Read (9)
3. Blueprint Mutation (16)
4. Actor Management (7)
5. Level Management (6)
6. Asset Editor (1)
7. Validation & Safety (3)
8. Visual Agent / Automation (13)
9. Transactions & Undo (4)
10. State Management (4)
11. Level Sequences (3)
12. Python Execution (1)
13. PIE Control (5)
14. Console Commands (4)
15. Input Simulation (1)
16. Audio (10)
17. Niagara VFX (11)
18. Pixel Streaming (7)
19. Meta XR (10)
20. Story/Game (4)
21. DataTable (2)
22. Animation (2)
23. Material (1)
24. Collision/Physics (1)
25. RenderDoc (1)

---

## MISSING CATEGORIES (exhaustive)

### Category A: Component Introspection & Manipulation
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 1 | /api/list-components | POST | List all components on an actor |
| 2 | /api/get-component | POST | Get component details (class, properties, transform) |
| 3 | /api/set-component-property | POST | Set a property on a component |
| 4 | /api/add-component-to-actor | POST | Add component to existing actor |
| 5 | /api/remove-component | POST | Remove component from actor |
| 6 | /api/get-component-transform | POST | Get component world/relative transform |
| 7 | /api/set-component-transform | POST | Set component world/relative transform |
| 8 | /api/attach-component | POST | Attach component to another component |
| 9 | /api/detach-component | POST | Detach component from parent |

### Category B: Asset Management & Content Browser
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 10 | /api/list-assets | POST | List assets by path/class/tag |
| 11 | /api/get-asset-metadata | POST | Get asset metadata (size, class, references, tags) |
| 12 | /api/import-asset | POST | Import external file as UE asset |
| 13 | /api/duplicate-asset | POST | Duplicate an asset |
| 14 | /api/rename-asset | POST | Rename/move an asset |
| 15 | /api/delete-asset | POST | Delete an asset |
| 16 | /api/save-asset | POST | Save a specific asset to disk |
| 17 | /api/save-all | POST | Save all dirty assets |
| 18 | /api/get-asset-dependencies | POST | Get what this asset depends on |
| 19 | /api/get-asset-referencers | POST | Get what references this asset |
| 20 | /api/asset-exists | POST | Check if asset path exists |
| 21 | /api/list-asset-paths | POST | List all content paths/folders |
| 22 | /api/create-folder | POST | Create content browser folder |

### Category C: World & Gameplay Queries
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 23 | /api/find-actors-by-class | POST | Find all actors of a given class |
| 24 | /api/find-actors-by-tag | POST | Find actors with specific tag |
| 25 | /api/find-actors-by-label | POST | Find actors by display label (fuzzy) |
| 26 | /api/get-world-info | POST | Get world settings, bounds, gravity, etc. |
| 27 | /api/get-world-bounds | POST | Get world bounding box |
| 28 | /api/raycast | POST | Cast ray from point in direction, return hits |
| 29 | /api/raycast-from-camera | POST | Cast ray from current camera forward |
| 30 | /api/sphere-overlap | POST | Sphere overlap test at location |
| 31 | /api/box-overlap | POST | Box overlap test at location |
| 32 | /api/get-actor-distance | POST | Distance between two actors |
| 33 | /api/line-of-sight | POST | Check line of sight between two points |

### Category D: Gameplay Framework
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 34 | /api/get-game-mode | POST | Get current game mode class and properties |
| 35 | /api/get-game-state | POST | Get game state properties |
| 36 | /api/get-player-controller | POST | Get player controller info |
| 37 | /api/get-player-pawn | POST | Get player pawn info (location, rotation, velocity) |
| 38 | /api/set-player-location | POST | Teleport player pawn |
| 39 | /api/set-player-rotation | POST | Set player view rotation |
| 40 | /api/get-all-player-controllers | POST | List all player controllers (PIE) |
| 41 | /api/get-hud | POST | Get HUD class info |
| 42 | /api/call-function-on-actor | POST | Call a BlueprintCallable function on an actor |
| 43 | /api/get-actor-variable | POST | Get a Blueprint variable value on an actor |
| 44 | /api/set-actor-variable | POST | Set a Blueprint variable value on an actor |

### Category E: Enhanced Input & VR Controller Simulation
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 45 | /api/input/list-actions | POST | List all Input Actions in project |
| 46 | /api/input/list-mappings | POST | List all Input Mapping Contexts |
| 47 | /api/input/inject-action | POST | Inject an input action trigger (grip, trigger, etc.) |
| 48 | /api/input/simulate-vr-controller | POST | Simulate VR controller position + button state |
| 49 | /api/input/simulate-hand-pose | POST | Simulate hand tracking pose |
| 50 | /api/input/get-action-bindings | POST | Get what keys/buttons map to an action |
| 51 | /api/input/simulate-motion-controller | POST | Simulate motion controller transform + buttons |
| 52 | /api/input/list-active-mappings | POST | List currently active mapping contexts |

### Category F: Sequencer / Level Sequence Control
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 53 | /api/sequence/play | POST | Play a specific level sequence by name/path |
| 54 | /api/sequence/stop | POST | Stop a playing sequence |
| 55 | /api/sequence/pause | POST | Pause a playing sequence |
| 56 | /api/sequence/set-time | POST | Set playback position (seconds or frame) |
| 57 | /api/sequence/get-time | POST | Get current playback position |
| 58 | /api/sequence/get-state | POST | Get sequence play state (playing/paused/stopped) |
| 59 | /api/sequence/get-length | POST | Get sequence duration |
| 60 | /api/sequence/list-tracks | POST | List all tracks in a sequence |
| 61 | /api/sequence/list-bindings | POST | List object bindings in a sequence |
| 62 | /api/sequence/add-track | POST | Add a track to a sequence |
| 63 | /api/sequence/add-key | POST | Add a keyframe to a track |
| 64 | /api/sequence/create | POST | Create a new level sequence asset |

### Category G: UMG / Widget / UI
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 65 | /api/ui/list-widgets | POST | List all active UMG widgets |
| 66 | /api/ui/get-widget-tree | POST | Get widget hierarchy tree |
| 67 | /api/ui/get-widget-property | POST | Get widget property (text, visibility, color) |
| 68 | /api/ui/set-widget-property | POST | Set widget property |
| 69 | /api/ui/create-widget | POST | Create and add widget to viewport |
| 70 | /api/ui/remove-widget | POST | Remove widget from viewport |
| 71 | /api/ui/simulate-ui-click | POST | Simulate click on UI element |

### Category H: Physics & Simulation
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 72 | /api/physics/set-simulate | POST | Enable/disable physics on actor |
| 73 | /api/physics/apply-force | POST | Apply force to physics body |
| 74 | /api/physics/apply-impulse | POST | Apply impulse to physics body |
| 75 | /api/physics/get-velocity | POST | Get linear/angular velocity |
| 76 | /api/physics/set-velocity | POST | Set linear/angular velocity |
| 77 | /api/physics/set-gravity | POST | Enable/disable gravity on actor |
| 78 | /api/physics/get-mass | POST | Get mass of physics body |
| 79 | /api/physics/set-mass | POST | Set mass override |
| 80 | /api/physics/set-collision-profile | POST | Set collision profile on component |
| 81 | /api/physics/get-collision-profile | POST | Get collision profile |
| 82 | /api/physics/set-collision-response | POST | Set per-channel collision response |
| 83 | /api/physics/simulate-step | POST | Step physics simulation by N frames |

### Category I: Timer Management
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 84 | /api/timer/set | POST | Set a timer (one-shot or looping) |
| 85 | /api/timer/clear | POST | Clear a timer by handle |
| 86 | /api/timer/list | POST | List all active timers |
| 87 | /api/timer/pause | POST | Pause a timer |
| 88 | /api/timer/resume | POST | Resume a paused timer |

### Category J: Landscape & Foliage
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 89 | /api/landscape/info | POST | Get landscape info (size, components, layers) |
| 90 | /api/landscape/get-height | POST | Get height at world XY |
| 91 | /api/landscape/get-layer-weight | POST | Get paint layer weight at location |
| 92 | /api/foliage/list-types | POST | List foliage types in level |
| 93 | /api/foliage/get-instances | POST | Get foliage instances in radius |
| 94 | /api/foliage/add-instance | POST | Add foliage instance |
| 95 | /api/foliage/remove-instances | POST | Remove foliage instances in radius |

### Category K: Lighting & Rendering
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 96 | /api/lighting/list-lights | POST | List all light actors |
| 97 | /api/lighting/set-property | POST | Set light property (intensity, color, radius) |
| 98 | /api/lighting/build | POST | Build lighting (preview/production) |
| 99 | /api/lighting/get-build-status | GET | Get lighting build progress |
| 100 | /api/rendering/set-scalability | POST | Set scalability settings |
| 101 | /api/rendering/get-stats | GET | Get rendering stats (FPS, draw calls, triangles) |
| 102 | /api/rendering/set-view-mode | POST | Set viewport view mode (lit, unlit, wireframe) |
| 103 | /api/rendering/set-show-flags | POST | Set show flags (bounds, collision, etc.) |
| 104 | /api/rendering/get-gpu-stats | GET | Get GPU timing stats |

### Category L: Editor Utilities & Build
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 105 | /api/editor/save-all | POST | Save all modified assets |
| 106 | /api/editor/build | POST | Build (geometry, paths, lighting, etc.) |
| 107 | /api/editor/cook | POST | Cook content for target platform |
| 108 | /api/editor/package | POST | Package project |
| 109 | /api/editor/get-preferences | POST | Get editor preferences |
| 110 | /api/editor/set-preference | POST | Set editor preference |
| 111 | /api/editor/get-project-settings | POST | Get project settings |
| 112 | /api/editor/set-project-setting | POST | Set project setting |
| 113 | /api/editor/reload-blueprints | POST | Force reload all blueprints |
| 114 | /api/editor/gc | POST | Force garbage collection |
| 115 | /api/editor/get-memory-stats | GET | Get memory usage stats |
| 116 | /api/editor/get-loaded-modules | GET | List loaded modules |
| 117 | /api/editor/hot-reload | POST | Trigger hot reload |

### Category M: Navigation & AI
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 118 | /api/nav/get-path | POST | Find navigation path between two points |
| 119 | /api/nav/is-reachable | POST | Check if point is reachable |
| 120 | /api/nav/get-random-point | POST | Get random navigable point |
| 121 | /api/nav/build | POST | Build/rebuild navigation mesh |
| 122 | /api/nav/get-bounds | POST | Get nav mesh bounds |
| 123 | /api/nav/project-point | POST | Project point to nav mesh |

### Category N: Skeletal Mesh & Animation (Extended)
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 124 | /api/anim/get-montage-state | POST | Get active montage info |
| 125 | /api/anim/play-montage | POST | Play animation montage |
| 126 | /api/anim/stop-montage | POST | Stop montage |
| 127 | /api/anim/set-variable | POST | Set anim blueprint variable |
| 128 | /api/anim/get-variable | POST | Get anim blueprint variable |
| 129 | /api/anim/list-montages | POST | List available montages |
| 130 | /api/anim/get-skeleton-info | POST | Get skeleton bone hierarchy |
| 131 | /api/anim/get-bone-transform | POST | Get bone world transform |
| 132 | /api/anim/set-bone-transform | POST | Override bone transform |
| 133 | /api/anim/list-anim-assets | POST | List animation assets |
| 134 | /api/anim/get-blend-space | POST | Get blend space info |
| 135 | /api/anim/get-state-machine | POST | Get anim state machine state |

### Category O: Perforce / Source Control
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 136 | /api/p4/status | POST | Get file P4 status (checked out, up to date, etc.) |
| 137 | /api/p4/checkout | POST | p4 edit a file |
| 138 | /api/p4/revert | POST | p4 revert a file |
| 139 | /api/p4/submit | POST | p4 submit with description |
| 140 | /api/p4/sync | POST | p4 sync files |
| 141 | /api/p4/diff | POST | p4 diff a file |
| 142 | /api/p4/history | POST | p4 filelog |
| 143 | /api/p4/lock | POST | p4 lock a file |
| 144 | /api/p4/unlock | POST | p4 unlock a file |
| 145 | /api/p4/opened | GET | List all currently checked out files |

### Category P: Texture & Material (Extended)
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 146 | /api/material/list-instances | POST | List material instances |
| 147 | /api/material/get-params | POST | Get all material parameters |
| 148 | /api/material/create-instance | POST | Create dynamic material instance |
| 149 | /api/material/set-texture | POST | Set texture parameter |
| 150 | /api/material/get-texture | POST | Get texture parameter |
| 151 | /api/texture/info | POST | Get texture info (size, format, mips) |
| 152 | /api/texture/import | POST | Import texture from file |

### Category Q: Blueprint Event Dispatch & Delegates
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 153 | /api/dispatch-event | POST | Fire a custom event on an actor by name |
| 154 | /api/call-blueprint-function | POST | Call a Blueprint function by name on actor |
| 155 | /api/list-custom-events | POST | List all custom events in a Blueprint |
| 156 | /api/list-event-dispatchers | POST | List all event dispatchers in a Blueprint |
| 157 | /api/bind-event | POST | Bind to an event dispatcher at runtime |
| 158 | /api/fire-event-dispatcher | POST | Fire an event dispatcher on an actor |

### Category R: Spline & Procedural
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 159 | /api/spline/get-points | POST | Get spline component points |
| 160 | /api/spline/set-points | POST | Set spline component points |
| 161 | /api/spline/add-point | POST | Add point to spline |
| 162 | /api/spline/get-length | POST | Get spline length |
| 163 | /api/spline/get-location-at-distance | POST | Get world location at distance along spline |
| 164 | /api/pcg/list-graphs | POST | List PCG graphs in level |
| 165 | /api/pcg/execute | POST | Execute a PCG graph |
| 166 | /api/pcg/get-settings | POST | Get PCG graph settings |

### Category S: Tags & Layers
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 167 | /api/tags/get-actor-tags | POST | Get tags on an actor |
| 168 | /api/tags/set-actor-tags | POST | Set tags on an actor |
| 169 | /api/tags/add-actor-tag | POST | Add a tag to an actor |
| 170 | /api/tags/remove-actor-tag | POST | Remove a tag from an actor |
| 171 | /api/tags/find-by-tag | POST | Find all actors with tag |
| 172 | /api/layers/list | POST | List all editor layers |
| 173 | /api/layers/set-actor-layer | POST | Set actor's layer |
| 174 | /api/layers/set-visibility | POST | Set layer visibility |

### Category T: World Partition & HLOD
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 175 | /api/wp/get-info | POST | Get World Partition info |
| 176 | /api/wp/list-data-layers | POST | List data layers |
| 177 | /api/wp/set-data-layer | POST | Set data layer state |
| 178 | /api/wp/list-regions | POST | List loaded regions |
| 179 | /api/wp/load-region | POST | Load a region |
| 180 | /api/hlod/build | POST | Build HLODs |
| 181 | /api/hlod/get-status | GET | Get HLOD build status |

### Category U: Clipboard & Copy/Paste
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 182 | /api/clipboard/copy-actors | POST | Copy selected actors to clipboard |
| 183 | /api/clipboard/paste-actors | POST | Paste actors from clipboard |
| 184 | /api/clipboard/copy-nodes | POST | Copy Blueprint nodes to clipboard |
| 185 | /api/clipboard/paste-nodes | POST | Paste Blueprint nodes from clipboard text |
| 186 | /api/clipboard/export-nodes-text | POST | Export nodes as paste text |

### Category V: Automation & Testing
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 187 | /api/test/list | GET | List available automation tests |
| 188 | /api/test/run | POST | Run an automation test |
| 189 | /api/test/get-results | GET | Get test results |
| 190 | /api/test/run-functional | POST | Run functional test map |
| 191 | /api/test/assert-actor-exists | POST | Assert actor exists in level |
| 192 | /api/test/assert-pin-connected | POST | Assert two pins are connected |
| 193 | /api/test/assert-node-exists | POST | Assert node exists in Blueprint |
| 194 | /api/test/validate-all-blueprints | POST | Compile and validate all Blueprints |

### Category W: Notifications & Logging
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 195 | /api/notify | POST | Show editor notification toast |
| 196 | /api/log | POST | Write to output log |
| 197 | /api/message-dialog | POST | Show modal message dialog |
| 198 | /api/get-message-log | POST | Get message log entries by category |

### Category X: Static Mesh & Geometry
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 199 | /api/mesh/info | POST | Get static mesh info (verts, tris, LODs, bounds) |
| 200 | /api/mesh/get-materials | POST | Get materials assigned to mesh |
| 201 | /api/mesh/set-material | POST | Set material on mesh slot |
| 202 | /api/mesh/get-bounds | POST | Get mesh bounding box |
| 203 | /api/mesh/list-lods | POST | List LOD info |
| 204 | /api/mesh/get-sockets | POST | List sockets on mesh |

### Category Y: Sound & Dialogue (Extended)
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 205 | /api/audio/list-cues | POST | List sound cues |
| 206 | /api/audio/list-waves | POST | List sound waves |
| 207 | /api/audio/get-attenuation | POST | Get sound attenuation settings |
| 208 | /api/audio/set-attenuation | POST | Set sound attenuation |
| 209 | /api/dialogue/list-voices | POST | List dialogue voices |
| 210 | /api/dialogue/play-line | POST | Play dialogue line |

### Category Z: Misc Engine
| # | Endpoint | Method | Purpose |
|---|----------|--------|---------|
| 211 | /api/get-engine-version | GET | Get UE version string |
| 212 | /api/get-platform-info | GET | Get platform info (OS, GPU, CPU) |
| 213 | /api/get-project-info | GET | Get project name, description, settings |
| 214 | /api/get-plugin-list | GET | List all enabled plugins |
| 215 | /api/enable-plugin | POST | Enable a plugin |
| 216 | /api/disable-plugin | POST | Disable a plugin |
| 217 | /api/get-config-value | POST | Read from .ini config files |
| 218 | /api/set-config-value | POST | Write to .ini config files |
| 219 | /api/file/read | POST | Read a file from project directory |
| 220 | /api/file/write | POST | Write a file to project directory |
| 221 | /api/file/list | POST | List files in project directory |
| 222 | /api/execute-editor-command | POST | Execute arbitrary editor command string |

---

## SUMMARY

| Status | Categories | Endpoints |
|--------|-----------|-----------|
| Already covered | 24 | 107 |
| Missing | 26 (A-Z) | 222 |
| **Total after expansion** | **50** | **329** |

---

## PRIORITY TIERS FOR SOH VR PROJECT

### Tier 1: CRITICAL for scene wiring + testing (implement first)
- **A: Component Introspection** (9) - Need to find GrabbableComponent, check connections
- **E: Enhanced Input / VR Simulation** (8) - Need to test VR interactions
- **F: Sequencer Control** (12) - Need to play/verify level sequences
- **Q: Blueprint Event Dispatch** (6) - Need to fire events, test chains
- **V: Automation & Testing** (8) - Need to validate wiring works
- **O: Perforce** (10) - Need p4 edit/submit from agent

**Tier 1 total: 53 new endpoints**

### Tier 2: HIGH VALUE for production workflow
- **B: Asset Management** (13)
- **C: World Queries** (11)
- **D: Gameplay Framework** (11)
- **H: Physics** (12)
- **U: Clipboard/Paste** (5)

**Tier 2 total: 52 new endpoints**

### Tier 3: NICE TO HAVE
- Everything else (117 endpoints)
