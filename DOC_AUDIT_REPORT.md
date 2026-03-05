# Documentation Audit & Reconciliation Report

**Author:** JARVIS
**Date:** 2026-03-05
**Version:** 1.0

---

## 1. Introduction

This report presents the findings of a comprehensive audit of the `ue56-level-editor` repository. The audit's purpose was to read every file, identify all contradictions, gaps, and redundancies across the documentation, and produce a clear path forward for establishing a single, coherent source of truth. The repository, while functional, contains a mixture of documents from different stages of development, leading to significant architectural confusion.

This report will first explain the core conflict at the heart of the documentation — the schism between two fundamentally different operational workflows. It will then detail the specific contradictions, gaps, and redundancies discovered, and conclude with a set of actionable recommendations to unify the project's documentation.

## 2. Executive Summary

The repository's documentation is split between two conflicting architectural models:

1.  **Workflow A: Headless Binary Editing.** This model, championed in `GROUND_TRUTH.md` [2] and proven effective by the K2Node parser/writer [6], involves direct, offline manipulation of `.uasset` files via the UAssetAPI bridge. It is powerful, fast, and does not require the Unreal Editor.

2.  **Workflow B: Interactive Scripting.** This model, detailed in the `README.md` [1] and `PLUGIN_EXPLAINER.md` [4], relies on generating Python scripts to be run inside a live Unreal Editor session, using a C++ plugin (`JarvisEditor`) to perform graph modifications.

The documentation is littered with outdated warnings against binary editing that have been proven false by the project's own recent breakthroughs in K2Node serialization. The result is a repository that appears to forbid the very thing it successfully achieves.

The primary recommendation is to **formally adopt Workflow A (Headless Binary Editing) as the single, canonical architecture.** The documentation must be rewritten to reflect this, demoting Workflow B to a secondary, local-only alternative. All warnings against binary editing should be removed and replaced with documentation for the now-proven K2Node parser and writer.

## 3. The Architectural Schism: Two Competing Workflows

The central issue is the co-existence of two completely different models for how the tool should operate. The documentation is written as if both are equally valid, or worse, as if the superior model (binary editing) is dangerously flawed.

| Aspect | Workflow A: Headless Binary Editing | Workflow B: Interactive Scripting |
| :--- | :--- | :--- |
| **Core Principle** | Modify `.uasset` files directly in memory using UAssetAPI. | Generate Python scripts to be run inside the Unreal Editor. |
| **Key Technology** | `uasset_bridge.py`, `parse_extras.py`, `k2node_writer.py` | `script_generator.py`, `JarvisEditor` C++ Plugin |
| **Editor Required?** | **No.** The entire process is offline. | **Yes.** Requires a live, running instance of the Unreal Editor. |
| **Speed** | Extremely fast (in-memory binary operations). | Slow (requires editor process, script execution, compilation). |
| **Safety** | Previously considered dangerous, but now **proven safe** with byte-perfect reconstruction tests [6]. | Considered safe because it uses the editor's own APIs. |
| **Primary Docs** | `GROUND_TRUTH.md` [2], `K2NODE_EXTRAS_RESEARCH.md` [6] | `README.md` [1], `PLUGIN_EXPLAINER.md` [4] |

This split is the source of every major contradiction in the repository.

## 4. Documentation Contradictions

The audit identified seven major contradictions stemming from the architectural schism.

| ID | Contradiction | Evidence & Analysis |
| :--- | :--- | :--- |
| **C1** | **"K2Node editing is dangerous" vs. "We did it successfully"** | Multiple documents, including `research_notes.md` [14] and `ue_assistant_response.md` [15], explicitly warn against binary editing of K2Nodes. However, the creation of `parse_extras.py` and `core/k2node_writer.py`, validated by byte-perfect reconstruction tests [6], proves these warnings are obsolete. The tool *can* and *does* safely edit K2Node data. |
| **C2** | **`script_generator.py` is "deprecated" but still central** | `GROUND_TRUTH.md` [2] states that `script_generator.py` is deprecated and will be removed. However, it remains in the repository, is a major component of the `README.md` [1], and is used by the API server. The deprecation was never carried out. |
| **C3** | **README claims Layer 1 is "Read-only"** | The main architecture diagram in `README.md` [1] describes the binary inspection layer as strictly read-only. This is false. The `uasset_bridge.py` has a comprehensive write API, and the core success of the project is its ability to write binary files. |
| **C4** | **`PLUGIN_EXPLAINER.md` claims binary editing is "guaranteed to cause crashes"** | This document [4] was written to justify the C++ plugin's existence by claiming the alternative (binary editing) was impossible. The success of the K2Node writer proves this foundational assumption is incorrect. |
| **C5** | **`GROUND_TRUTH.md` claims "There is no script generation"** | This document [2] makes a definitive statement that is factually untrue for the repository in its current state. Both `core/script_generator.py` and `scripts/soh_inject_interactions.py` [18] exist and are tracked in git. |
| **C6** | **`AUDIT.md` is outdated** | The audit from March 4th [5] states the write path is "limited" and missing key features like adding exports or K2Nodes. These features have since been implemented. |
| **C7** | **Two conflicting Epic Assistant responses** | The repository contains two separate documents (`ue_assistant_response.md` [15] and `epic_assistant_confirmation.md` [10]) that offer conflicting advice. The former warns of danger, while the latter confirms the stability of the binary format, which enabled the creation of the successful parser/writer. |

## 5. Documentation Gaps

The audit identified several areas where documentation is missing entirely.

- **G1: No Updated Architecture Diagram:** The primary diagram in the `README.md` is obsolete. It fails to represent the now-central K2Node binary editing pipeline.
- **G2: No Docs for Core Parser/Writer:** The most critical new components, `parse_extras.py` and `core/k2node_writer.py`, have no dedicated documentation explaining their APIs or usage.
- **G3: No Docs for Test Level Builder:** The `build_test_level.py` script, which serves as the primary example of how to use the new binary writers, is undocumented.
- **G4: Incomplete File Structure in README:** The file manifest in the `README.md` is missing over a dozen new and critical files, including all the K2Node-related code and research documents.
- **G5: No Project CHANGELOG:** There is no central changelog to track the project's rapid evolution, making it difficult to understand when and why architectural decisions were made.
- **G6: Incorrect SOH Mechanics Mapping:** The `SOH_MECHANICS_CATALOG.md` [7] contains an incorrect mapping of scenes to level files, contradicting the `soh_inject_interactions.py` script [18].

## 6. Documentation Redundancies

The audit found significant overlap and redundancy, creating multiple, conflicting sources of truth.

- **R1: K2Node Format in 3 Places:** The binary format is described in the research log [6], the clean reference doc [8], and a subclass-specific doc [9].
- **R2: Architecture in 4 Places:** The overall architecture is described differently in the `README.md` [1], `GROUND_TRUTH.md` [2], `MANUS_INTEGRATION.md` [3], and `PLUGIN_EXPLAINER.md` [4].
- **R3: Epic Assistant Responses in 2 Places:** Two separate files [10, 15] contain overlapping and partially contradictory advice from the Epic UE Assistant.
- **R4: Audit Findings in 2 Places:** Outdated audit findings exist in both `AUDIT.md` [5] and `debug_findings.md` [12].

## 7. Recommendations

To resolve these issues, the following actions are recommended:

1.  **Establish `GROUND_TRUTH.md` as the Single Source of Truth:** This document should be updated to reflect the success of the binary editing workflow (Workflow A). It should be the definitive architectural guide.

2.  **Rewrite `README.md`:** The main `README.md` must be rewritten to align with the headless, binary-first architecture. The 3-layer diagram should be replaced with one that highlights the UAssetAPI bridge and the K2Node parser/writer. Workflow B (scripting) should be demoted to a small, secondary section for local use cases.

3.  **Consolidate and Archive Old Docs:**
    -   Merge the key findings from all research and audit documents (`K2NODE_EXTRAS_RESEARCH.md`, `AUDIT.md`, `debug_findings.md`, etc.) into a new `docs/archive/` directory.
    -   The primary `k2node_binary_format.md` should be the only top-level reference for the binary format.
    -   Consolidate the two Epic Assistant responses into a single, coherent document.

4.  **Create New Documentation:**
    -   Create a new document, `BINARY_EDITING_API.md`, that details the APIs of `parse_extras.py` and `core/k2node_writer.py`.
    -   Add a `CHANGELOG.md` to the root of the repository.
    -   Update the file manifest in the `README.md` to be complete and accurate.

5.  **Correct Factual Errors:**
    -   Update the `SOH_MECHANICS_CATALOG.md` [7] to match the logic in the `soh_inject_interactions.py` script [18].

## 8. Conclusion

The `ue56-level-editor` project has achieved a significant technical breakthrough: the ability to safely and reliably edit Unreal Engine Blueprint graphs at a binary level. However, its documentation has not kept pace with its own success. The current state of the docs is confusing and contradictory, actively warning against the very methods that make the tool powerful.

By adopting the recommendations in this report, the repository's documentation can be unified, clarified, and brought into alignment with the project's actual, proven capabilities. This will ensure that future development is built on a solid, coherent, and accurate foundation.

---

## 9. References

[1] `README.md`
[2] `GROUND_TRUTH.md`
[3] `MANUS_INTEGRATION.md`
[4] `PLUGIN_EXPLAINER.md`
[5] `AUDIT.md`
[6] `K2NODE_EXTRAS_RESEARCH.md`
[7] `SOH_MECHANICS_CATALOG.md`
[8] `k2node_binary_format.md`
[9] `k2node_subclass_extras.md`
[10] `epic_assistant_confirmation.md`
[11] `UE_MCP_RAID_NOTES.md`
[12] `debug_findings.md`
[13] `metadata_fix_notes.md`
[14] `research_notes.md`
[15] `ue_assistant_response.md`
[16] `restaurant_structure.md`
[17] `restaurant_imports.txt`
[18] `scripts/soh_inject_interactions.py`
