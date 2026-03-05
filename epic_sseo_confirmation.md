# Epic UE Assistant - SSEO Confirmation (2026-03-05)

## Question

In UE 5.6, I'm programmatically editing .umap files using UAssetAPI. I discovered
that when I modify tagged properties on K2Node exports (changing their serialized
size), UAssetAPI correctly recalculates SerialSize in the export table but does NOT
recalculate ScriptSerializationEndOffset. This causes the engine to assert during
deserialization. Two questions:

1. How is ScriptSerializationEndOffset computed? Is it simply the byte offset from
   the start of the export's serial data to where tagged properties end (i.e., the
   None terminator)?
2. Is the export table entry layout in UE 5.6 still 112 bytes per entry with
   ScriptSerializationEndOffset at field offset +100?

## Answer (verbatim)

In UE 5.6 export table entries within .umap files:

1. **ScriptSerializationEndOffset** generally marks the byte offset (from the start
   of the export's serialized data) right after the tagged property serialization
   ends -- typically just past the `None` terminator that signals the end of tagged
   properties. It is a subset boundary inside the total serialized data (tracked by
   `SerialSize`), used by the engine to delimit script (property) serialization. So
   yes, your understanding is mostly correct.

2. Regarding the export table entry size and field layout: Historically, the export
   table entry has been approximately 112 bytes in UE builds around 4.x to 5.x
   series, with the `ScriptSerializationEndOffset` field located near offset +100
   bytes. However, this layout can vary between UE versions and engine branches. For
   UE 5.6 specifically, it is very likely similar, but **exact offsets should be
   verified against the UE5.6 source code** (in `FObjectExport` structure in
   `UObjectGlobals.h` or related serialization code).

**To clarify for your use case:**

- If you modify tagged properties and their serialized size changes, you must update
  _both_ `SerialSize` and `ScriptSerializationEndOffset` accordingly.
- Failure to update `ScriptSerializationEndOffset` leads to deserialization asserts
  because the engine expects tagged properties serialization to end exactly at that
  offset.
- Typically, `ScriptSerializationEndOffset` = offset of the final `None` property
  terminator + size of terminator data, relative to the export data start.

## Our Findings

- Confirmed: SSEO = byte offset from export serial data start to end of tagged
  properties (past None terminator)
- Confirmed: Export table entry is 112 bytes in our UE 5.6 Oculus fork
- Confirmed: SSEO is at field offset +100 within the export entry
- UAssetAPI bug: recalculates SerialSize but NOT SSEO on save
- Our post_save_patcher.py fixes this for K2Node exports using the formula:
  SSEO = SerialSize - ExtrasSize - 4
- This formula works for all K2Node exports but NOT for BlueprintGeneratedClass,
  Level, or CDO exports (which have additional serialization after SSEO)
