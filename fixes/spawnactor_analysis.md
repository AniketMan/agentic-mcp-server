# SpawnActor API Analysis for UE 5.6

## Current State in Codebase

The codebase has two patterns:

### Pattern A (Handlers_Actors.cpp - already fixed):
```cpp
FTransform SpawnTransform(FRotator(...).Quaternion(), Location, Scale);
AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
```
This passes FTransform by VALUE (no pointer). This should work.

### Pattern B (12 other files - NOT fixed):
```cpp
World->SpawnActor<T>(T::StaticClass(), &SpawnTransform, SpawnParams);
```
This passes FTransform by POINTER (&SpawnTransform). In UE 5.6, the pointer-to-FTransform overload
`SpawnActor(UClass*, const FTransform*, FActorSpawnParameters)` was deprecated.

## Files needing fix (Pattern B - pointer to FTransform):
1. Handlers_Audio.cpp:571 - &SpawnTransform
2. Handlers_Audio.cpp:617 - &VolumeTransform
3. Handlers_ChaosDestruction.cpp:229 - &SpawnTransform
4. Handlers_Composure.cpp:115 - &SpawnTransform
5. Handlers_Lighting.cpp:65,67,69,71,73 - &SpawnTransform (5 instances)
6. Handlers_MetaHuman.cpp:83 - &SpawnTransform
7. Handlers_Physics.cpp:327 - (need to check)
8. Handlers_VCam.cpp:104 - &SpawnTransform
9. Handlers_WaterSystem.cpp:108 - &SpawnTransform

## Fix: Remove the & (address-of) operator to pass FTransform by value instead of pointer
