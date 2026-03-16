# FActorFolders / FFolder Constructor Fix (UE 5.6)

## Problem

In UE 5.6, the `FFolder` constructor no longer accepts a single `FName` parameter. The old constructor:

```cpp
// REMOVED in UE 5.6
FFolder(FName InPath);
```

Has been replaced with a two-parameter constructor requiring a root object:

```cpp
// UE 5.6 required signature
FFolder(const FRootObject& InRootObject, const FName& InPath);
```

The `FRootObject` is typically the `PersistentLevel` of the world.

Additionally, the include `"ActorFolders.h"` was changed to `"EditorActorFolders.h"` in UE 5.6.

## Fix

In `Handlers_SceneHierarchy.cpp`:

1. Changed `#include "ActorFolders.h"` to `#include "EditorActorFolders.h"` and added `#include "Folder.h"`
2. Both `CreateFolder` and `DeleteFolder` calls now construct `FFolder` with the world's `PersistentLevel` as the root object:

```cpp
FFolder::FRootObject RootObject(World->PersistentLevel);
FActorFolders::Get().CreateFolder(*World, FFolder(RootObject, FName(*FolderPath)));
```

## Files Changed

- `Source/AgenticMCP/Private/Handlers_SceneHierarchy.cpp` -- Lines 22-24 (includes), 274-275 (CreateFolder), 314-315 (DeleteFolder)
