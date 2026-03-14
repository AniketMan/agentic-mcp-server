# Example Project Config: SOH (Ordinary Courage VR)

*This is an example of what a user might put in the `user_context/` directory to give the Planner and Workers project-specific rules.*

## Scene Mapping
| Scene | Master Level | Sublevel (Logic) | Sublevel (Art) | Sequence |
|---|---|---|---|---|
| 00_Intro | ML_Intro | SL_Intro_Logic | SL_Intro_Art | LS_Intro |
| 01_Apartment | ML_Apartment | SL_Main_Logic | SL_Apartment_Art | LS_Apt_Start |
| 02_Hospital | ML_Hospital | SL_Main_Logic | SL_Hospital_Art | LS_Hosp_Start |
| 03_Therapy | ML_Therapy | SL_Main_Logic | SL_Therapy_Art | LS_Therapy_Start |
| 04_Dream | ML_Dream | SL_Main_Logic | SL_Dream_Art | LS_Dream_Start |
| 05_Restaurant | ML_Restaurant | SL_Restaurant_Logic | SL_Restaurant_Art | LS_Rest_Start |

## Asset Paths
- **Player Pawn:** `/Game/Blueprints/Player/BP_VRPawn`
- **Interaction Interface:** `/Game/Blueprints/Interfaces/BPI_Interactable`
- **Game Instance:** `/Game/Blueprints/Core/GI_SOH`

## Specific Rules
1. **Scene 5 Python Only:** Scene 05 (Restaurant) logic must be wired exclusively using the Python execution tool. Do not use C++ node mutation tools on SL_Restaurant_Logic.
2. **Music Loop Directive:** Every scene must have an ambient audio track that starts on `BeginPlay` and loops continuously.
3. **makeTempBP Rule:** When creating a temporary Blueprint, always add a `StaticMeshComponent` and set its collision profile to `BlockAllDynamic`.

## Interaction Chain Pattern
When wiring a grabbable object:
1. Load `BP_VRPawn`
2. Find the `GripGrab` event
3. Connect it to a `Cast To BPI_Interactable` node
4. Connect the success pin to `Execute_OnGrabbed`
5. Connect the target pin to the hit actor from a sphere trace
