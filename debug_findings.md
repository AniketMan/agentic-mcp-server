# Root Cause: SL_Restaurant_Logic.umap Round-Trip Failure

## The Bug

`GetNameReferenceWithoutZero()` in UAsset.cs:
```csharp
public FString GetNameReferenceWithoutZero(int index)
{
    FixNameMapLookupIfNeeded();
    if (index <= 0) return new FString(Convert.ToString(-index));
    if (index >= nameMapIndexList.Count) return new FString(Convert.ToString(index));
    return nameMapIndexList[index];
}
```

When an FName index is **out of range** (e.g., 399 when the name map only has 305 entries),
UAssetAPI silently converts the index to a string: `new FString("399")`.

This doesn't crash on read. But on write, when it tries to serialize this FName,
it calls `AddNameReference("399")` which fails because `isSerializationTime` is true.

## Why This Happens

The `FPropertyTypeNameNode` in the K2Node exports reads FNames using `ReadFName()`.
Some of these FNames have indices (like 399) that exceed the name map size (305).

This is likely because:
1. The Oculus 5.6 fork serializes K2Node property type names differently
2. OR these are **global** FName references (not package-local) that UAssetAPI doesn't handle
3. OR the binary data at these positions isn't actually FName data — it's being misinterpreted

## Raw Binary Evidence

At offset 90714 in SL_Restaurant_Logic.umap:
- Property Name: index=290 (valid, "K2Node_CustomEvent_Scene5_TeleportToHeather")
- Type Name: index=399 (OUT OF RANGE), number=0, innerCount=1
- Inner node[0]: index=209 (valid, "VertexIndices"), number=0, innerCount=1
- Inner node[1]: index=??? (from innerCount=1 of node[0])

## Fix Strategy

Option A: Pre-add missing names to the name map before serialization
Option B: Patch UAssetAPI to handle out-of-range FName indices during write
Option C: Skip the problematic properties during round-trip (preserve as raw bytes)
