# UE 5.6/5.7 API Compatibility Fixes

All fixes use `ENGINE_MAJOR_VERSION` / `ENGINE_MINOR_VERSION` preprocessor guards so the plugin compiles on UE 5.4 through 5.7+.

## Fix 1: SpawnActor API (Handlers_Actors.cpp:314)

**Problem:** The old `SpawnActor(Class, &Location, &Rotation, Params)` pointer overload was removed or changed in UE 5.6.

**Fix:** Switched to the `FTransform` reference overload which exists in all UE 5.x versions:
```cpp
FTransform SpawnTransform(FRotator(...).Quaternion(), Location, Scale);
AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
```

No version guard needed — this overload is universal.

## Fix 2: EditorLevelUtils::AddLevelToWorld (Handlers_Level.cpp:159)

**Problem:** UE 5.6+ replaced the 3-param overload with `UEditorLevelUtils::AddLevelToWorld(World, FAddLevelToWorldParams)`.

**Fix:** Version-guarded:
```cpp
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
    UEditorLevelUtils::FAddLevelToWorldParams AddParams;
    AddParams.LevelPackageName = *LevelPath;
    AddParams.LevelStreamingClass = ULevelStreamingDynamic::StaticClass();
    NewLevel = UEditorLevelUtils::AddLevelToWorld(World, AddParams);
#else
    NewLevel = EditorLevelUtils::AddLevelToWorld(World, *LevelPath, ULevelStreamingDynamic::StaticClass());
#endif
```

## Fix 3: FBlueprintEditorUtils::AddFunctionGraph (Handlers_Mutation.cpp:1099)

**Problem:** UE 5.6+ requires a 4th parameter `SignatureFromObject` (template `<typename SignatureType>`).

**Fix:** Version-guarded:
```cpp
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
    FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false, static_cast<UFunction*>(nullptr));
#else
    FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false);
#endif
```

## Fix 4: Missing BlueprintFactory Include (Handlers_Mutation.cpp)

**Problem:** `UBlueprintFactory` was used without including its header.

**Fix:** Added:
```cpp
#include "Factories/BlueprintFactory.h"
```

## Fix 5: BP->ErrorsFromLastCompile Removed (Handlers_Mutation.cpp:1259)

**Problem:** `ErrorsFromLastCompile` property was removed from `UBlueprint` in UE 5.6.

**Fix:** Replaced with `BP->Status` enum check (works on all versions) plus version-guarded diagnostics:
```cpp
// UE 5.6+: Use BP->Status as authoritative result
// UE 5.4-5.5: Also iterate ErrorsFromLastCompile for detailed messages
```

The compile endpoint now returns a `diagnostics` array and a `status` string field.

## Additional Fix: .uplugin FriendlyName

Changed `"FriendlyName": "Manus MCP"` to `"FriendlyName": "AgenticMCP"`.
