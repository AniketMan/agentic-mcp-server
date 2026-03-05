# Unreal Engine Assistant Response — Binary Asset Editing

## 1) Key Serialization Changes — PROPERTY_TAG_COMPLETE_TYPE_NAME & METADATA_SERIALIZATION_OFFSET

### PROPERTY_TAG_COMPLETE_TYPE_NAME

Introduced in UE 5.4+ to serialize the full C++ type name of UObject properties (complete types). The driver is more strict and explicit property typing for versioning.

- You must replicate exact UTF-8 strings of full type names for property tags or deserialization will fail or mismatch.
- Any alterations (case, whitespace, extra qualifiers) will break deserialization or cause silent data loss.
- This also affects downstream custom serializers and reflections relying on precise type lookup.

### METADATA_SERIALIZATION_OFFSET

This controls the serialization of metadata annotations (editor-only, tooltips, etc) on UProperties or UObjects. From 5.4 onward, some metadata moved to serialized blobs with offsets referencing them, shifting property offsets internally.

- You must track the correct offset values of metadata blobs or risk misalignment of subsequent property data.
- Failure to properly update METADATA_SERIALIZATION_OFFSET values can cause deserialization to read wrong data at shifted offsets, causing crashes or corruption.
- Metadata blobs may not affect gameplay but are crucial for editor correctness.

Both changes increase serialization rigidity and complexity, requiring exact byte-level reconstruction of property tags and offsets during binary edits.

## 2) Critical Export Fields for Adding Actors to .umap LevelExport

### 2) Critical Export Fields for Adding Actors to .umap LevelExport

- **ClassIndex / ClassPackageIndex**: Must point to the correct actor or subclass via FPackageIndex. Failure resolving class on load causes crashes or game module issues.
- **ObjectName**: Unique within the package; must not conflict with existing exports. Usually formatted as a named instance like `Actor_12`.
- **OuterIndex**: Must point to the export representing the owning ULevel object (persistent level), ensuring proper hierarchy.
- **SerialSize / SerialOffset**: Correct size and offset values for the export's payload in the bulk data stream. Incorrect values cause out-of-bounds reads or malformed loading.
- **ObjectFlags**: Proper flags for the actor (e.g., RF_Public, RF_Transactional). Missing required flags may cause engine asserts.
- **Template / Archetype references** (optional but recommended): If the Actor uses archetype templates for default values, link these correctly to maintain proper property initialization.
- **Property Data Payload**: Must match expected serialized property layouts exactly for the given Actor class, respecting serialization changes (see point 1).

Regarding OFPA (One File Per Actor), if enabled, actors may have external packages; references in LevelExports must correspond correctly to their external packages and streaming behavior.

### 3) Known Gotchas in K2Node/Kismet Bytecode Serialization & Extras Blob

- **Extras blob reliability**: The Extras blob is a binary serialization container that stores additional bytecode, debugging, and node metadata for Blueprints' K2Nodes. It is complex and partially undocumented.
- Modifying Extras blob arbitrarily often corrupts node references or link indices.
- Bytecode offsets and references inside it are sensitive to graph structure changes; adding/removing nodes requires recalculating offsets.
- Binary editing requires a deep understanding of FKismetCompilerBackend's output and node references.

### Caveats and Precautions

- Unreal's asset serialization is complex and version-specific; precise byte-level matches are mandatory for round-trip integrity.
- 3rd party binary editing tools risk package corruption or crashes if structural invariants break.
- Always back up assets and test under multiple engine versions.
- Consider using official C++/Blueprint APIs or commandlets for graph/state changes as safer alternatives.

### Stability of Serialization Format

- Epic does NOT guarantee backward/forward compatibility of binary format between UE 5.4-5.6; internal changes can alter layout.
- Custom version-dependent entries that are not exposed to high-level APIs.
- Use the content compiler pipeline if possible (commandlet or command line) for reliable results.
- Focus on fixed-structure bulk properties, not graph bytecode internals unless you understand Kismet logic.

## Takeaway for Our Tool

1. **Property editing is SAFE** — we can modify existing property values and add simple properties with confidence
2. **Adding actors to levels is SAFE** — as long as we set ClassIndex, OuterIndex, ObjectName, ObjectFlags, SerialSize/Offset correctly (which we do)
3. **K2Node/Kismet bytecode editing is DANGEROUS** — the Extras blob is undocumented and fragile. We should NOT attempt to modify Blueprint graph logic via binary editing. This confirms the JarvisEditor plugin is the right approach for graph editing.
4. **OFPA (One File Per Actor)** — need to check if SOH_VR uses this; if so, actor references need external package handling
