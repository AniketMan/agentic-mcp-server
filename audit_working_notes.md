# Audit Working Notes

## Files Read (37 total tracked files)

### Documentation (13 files)
1. README.md (857 lines) - Main repo docs, architecture, API ref, plugin ref, dashboard
2. GROUND_TRUTH.md (112 lines) - "Source of truth" for architecture
3. MANUS_INTEGRATION.md (98 lines) - Agent workflow guide
4. PLUGIN_EXPLAINER.md (108 lines) - JarvisEditor C++ plugin explainer
5. AUDIT.md (43 lines) - Mar 04 codebase audit
6. K2NODE_EXTRAS_RESEARCH.md (299 lines) - Reverse engineering research log
7. k2node_binary_format.md (91 lines) - Binary format reference
8. k2node_subclass_extras.md (73 lines) - Subclass-specific extras
9. PROJECT_MECHANICS_CATALOG.md (66 lines) - Project interaction catalog
10. epic_assistant_confirmation.md (59 lines) - Epic UE assistant confirmation
11. UE_MCP_RAID_NOTES.md (80 lines) - Competitive analysis of UE-MCP
12. debug_findings.md (44 lines) - FName out-of-range bug
13. metadata_fix_notes.md (19 lines) - MetaData section fix
14. research_notes.md (21 lines) - Early research notes
15. ue_assistant_response.md (63 lines) - Earlier Epic assistant response
16. restaurant_structure.md (25 lines) - Restaurant map structure
17. restaurant_imports.txt (26 lines) - Restaurant import table dump

### Code (14 files)
1. core/uasset_bridge.py (995 lines) - Python <-> UAssetAPI bridge
2. core/blueprint_editor.py (1030 lines) - K2 node resolution, bytecode
3. core/level_logic.py (365 lines) - Actor enumeration
4. core/integrity.py (386 lines) - Reference validation
5. core/plugin_validator.py (712 lines) - Plugin validation
6. core/project_scanner.py (814 lines) - Project scanning
7. core/script_generator.py (1410 lines) - UE Python script generation
8. core/k2node_writer.py (804 lines) - K2Node writer
9. core/cli.py (332 lines) - CLI
10. parse_extras.py (369 lines) - K2Node Extras parser
11. build_test_level.py (777 lines) - Test level builder
12. test_reconstruction.py (281 lines) - Reconstruction test
13. test_write_extras.py (211 lines) - Write extras test
14. scripts/project_inject_interactions.py (433 lines) - Project injection script (Workflow B)

### UI (2 files)
1. ui/server.py (1049 lines) - Flask API
2. ui/static/index.html (2320 lines) - Dashboard SPA

### Plugin (6 files)
1. ue_plugin/JarvisEditor/JarvisEditor.uplugin
2-6. Source files (.h, .cpp, .Build.cs)

### Infrastructure (4 files)
1. setup.sh (169 lines)
2. run_dashboard.py (37 lines)
3. requirements.txt (2 lines)
4. .gitignore (11 lines)
5. __main__.py (5 lines)

---

## CONTRADICTIONS FOUND

### C1: "K2Node editing is DANGEROUS" vs "We did it successfully"

**research_notes.md** (line 18-19):
> "K2Node/Kismet bytecode editing is NOT supported (too complex, too risky)"
> "We should NOT attempt to modify Blueprint graph logic via binary editing"

**ue_assistant_response.md** (line 62):
> "K2Node/Kismet bytecode editing is DANGEROUS... We should NOT attempt to modify Blueprint graph logic via binary editing"

**GROUND_TRUTH.md** (line 106):
> "core/script_generator.py is deprecated and will be removed"

BUT: We literally just built `parse_extras.py`, `core/k2node_writer.py`, `build_test_level.py`, and `test_reconstruction.py` that DO exactly this — and they work. 56/56 byte-perfect reconstruction. 10/10 on test level with injected nodes. Epic confirmed the format is stable.

**VERDICT:** The early docs were written before the breakthrough. They are now wrong. The K2Node Extras blob is fully decoded and writable.

### C2: "script_generator.py is deprecated" vs it's still in the repo

**GROUND_TRUTH.md** (line 106):
> "core/script_generator.py is deprecated and will be removed"

But it's still tracked, still imported by server.py, still has endpoints. README.md documents it extensively (45 operations across 10 domains).

**VERDICT:** It was never removed. It's still useful for Workflow B. The deprecation statement was premature.

### C3: README architecture diagram says "Read-only — never writes to asset files" for Layer 1

**README.md** (line 33):
> "Read-only — never writes to asset files."

But `uasset_bridge.py` has full write operations: `add_export()`, `add_import()`, `add_actor_to_level()`, `remove_actor_from_level()`, `add_property()`, `remove_property()`, `set_property_value()`, `save()`. And we just built a test level using binary writes.

**VERDICT:** README Layer 1 description is outdated. The bridge now reads AND writes.

### C4: PLUGIN_EXPLAINER.md says binary editing is "extremely dangerous and guaranteed to cause crashes"

**PLUGIN_EXPLAINER.md** (line 26):
> "Attempting to do so would require manually manipulating the binary .uasset file, which is extremely dangerous and guaranteed to cause crashes and data corruption."

This was written before the K2Node parser/writer was built. It's now provably false — we have byte-perfect round-trips.

**VERDICT:** Outdated. The statement was correct at the time of writing but is no longer accurate.

### C5: GROUND_TRUTH says "There is no script generation" but scripts still exist

**GROUND_TRUTH.md** (line 14):
> "There is no script generation. There is no -ExecutePythonScript."

But `scripts/project_inject_interactions.py` exists and is tracked. `core/script_generator.py` exists and is tracked.

**VERDICT:** GROUND_TRUTH was aspirational, not descriptive. The repo has both workflows.

### C6: AUDIT.md says write path is "limited" — now it's comprehensive

**AUDIT.md** (line 8):
> "MISSING: No functions for adding/removing exports, adding/removing K2 nodes at the binary level"

All of these now exist in `uasset_bridge.py` and the K2Node writer.

**VERDICT:** AUDIT.md is from Mar 04 and is now outdated.

### C7: Two separate "Epic assistant response" docs

- `ue_assistant_response.md` — earlier response about property editing safety
- `epic_assistant_confirmation.md` — later response confirming K2Node format stability

These don't contradict each other but they overlap and the earlier one says K2Node editing is dangerous, while the later one confirms the format is stable and writable.

**VERDICT:** Should be consolidated or the earlier one should be annotated.

---

## GAPS FOUND

### G1: No updated architecture diagram
The README architecture diagram (3-layer model) doesn't mention the K2Node parser/writer layer at all. There's no mention of `parse_extras.py`, `k2node_writer.py`, or `build_test_level.py`.

### G2: No docs for parse_extras.py or test_reconstruction.py
These are critical files with no standalone documentation. K2NODE_EXTRAS_RESEARCH.md describes the format but not the API of the parser.

### G3: No docs for build_test_level.py
The test level builder is undocumented.

### G4: File Structure in README is incomplete
README's File Structure section (line 814-849) doesn't list:
- parse_extras.py
- core/k2node_writer.py
- build_test_level.py
- test_reconstruction.py
- test_write_extras.py
- scripts/project_inject_interactions.py
- Any of the research/audit docs

### G5: No CHANGELOG or version history
The repo has no changelog. The only version tracking is inside K2NODE_EXTRAS_RESEARCH.md.

### G6: PROJECT_MECHANICS_CATALOG has wrong scene-to-file mapping
Line 63-66 says:
- SL_Trailer_Logic: 8 interactions (Scenes 3-6)
- SL_Restaurant_Logic: 2 interactions (Scene 3 subset)

But the injection script says:
- SL_Trailer_Logic: 8 interactions (Scenes 3-4)
- SL_Restaurant_Logic: 3 interactions (Scene 5)

These don't match.

---

## REDUNDANCIES FOUND

### R1: K2Node format documented in 3 places
- K2NODE_EXTRAS_RESEARCH.md (most detailed, research log format)
- k2node_binary_format.md (clean reference format)
- k2node_subclass_extras.md (subclass-specific only)

### R2: Architecture described in 4 places
- README.md (3-layer diagram + full API ref)
- GROUND_TRUTH.md (2-workflow model)
- MANUS_INTEGRATION.md (Manus-specific workflow)
- PLUGIN_EXPLAINER.md (plugin-specific)

### R3: Epic assistant responses in 2 places
- ue_assistant_response.md (earlier, general)
- epic_assistant_confirmation.md (later, K2Node-specific)

### R4: Audit findings in 2 places
- AUDIT.md (Mar 04, outdated)
- debug_findings.md (specific bug, resolved)
