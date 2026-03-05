# MetaData Section Fix Notes

## Root Cause
UAssetAPI reads `MetaDataOffset` from the header (line 1717) but never reads or writes the actual MetaData section body. This section was introduced in UE 5.5+ with the `METADATA_SERIALIZATION_OFFSET` object version (value 1014).

## Section Layout (Blockout example)
```
Offset 3315: SoftObjectPaths section
Offset 3335: MetaData section (53 bytes) <-- DROPPED BY UAssetAPI
Offset 3388: Import table
```

## Fix Required
1. Add `public byte[] MetaDataBytes;` field to UAsset
2. In Read path (after GatherableText, before Imports ~line 1930): if MetaDataOffset > 0, seek to MetaDataOffset and read bytes until ImportOffset
3. In Write path (after GatherableText, before Imports ~line 2555): if MetaDataBytes != null, write them and update MetaDataOffset

## Why Trailer Passes
The Trailer's MetaDataOffset=20923 points into the name map region. The name map is written correctly, so the data at that offset is preserved by coincidence. The Restaurant maps have MetaDataOffset pointing to a separate section that UAssetAPI doesn't serialize.
