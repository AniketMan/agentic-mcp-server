# The Adaptive Quantized Asset & AI-Orchestrated Logic Pipeline
## Automating Unreal Engine Development with Claude and MCP

**Author:** JARVIS
**Date:** March 2026

### 1. The Division of Labor: Data vs. Intent

When building a game or interactive VR experience (like the *Ordinary Courage VR* script), the project is composed of two fundamentally different elements:

1.  **Spatial/Audio Data (The Assets):** Meshes, textures, materials, animations, and audio files. This is pure data. It can and should be **adaptively quantized**.
2.  **Semantic Intent (The Logic):** The script, the cause-and-effect relationships, the event triggers, and the game flow. This is not spatial data; it is human intent. This requires a **Language Model (LLM)** to interpret and wire together.

The ideal pipeline does not use Generative AI to create assets. Instead, it uses an LLM (like Claude) operating through a Model Context Protocol (MCP) server to wire together existing, quantized assets based on a natural language script.

### 2. Adaptively Quantizing the Project Inputs (The Asset Manifest)

Before Claude can build anything, it needs to know what exists in the project. We do not feed the actual binary assets into the LLM. Instead, we generate an **Adaptive Quantized Asset Manifest**. This manifest scales the token budget based on the asset's importance (e.g., a hero character gets a detailed structural breakdown, while a background prop gets a single line).

This manifest is a lightweight, structured database (JSON or text) that describes every asset in the project:

*   **Static Meshes:** `SM_Teacup`, `SM_KitchenTable`, `SM_HospitalDoor` (includes bounding box size and pivot location).
*   **Skeletal Meshes & Animations:** `SK_Heather_Adult`, `Anim_Heather_Walk`.
*   **Audio:** `SFX_Heartbeat_Fast`, `VO_Susan_Line01`.
*   **Logic Components:** `GrabComponent`, `GazeTargetComponent`, `TeleportMarker`.

This manifest is the "quantized input" for the LLM. It represents the entire 2TB project drive as a few kilobytes of text.

### 3. How Claude + MCP Wires the Logic

With the Asset Manifest and the MCP tool definitions (which allow Claude to execute Unreal Python/Blueprint commands), Claude acts as a Technical Director.

**Example 1: "Put the teacup on the table and make it grabbable."**
1.  **Parse Intent:** Claude reads the instruction.
2.  **Query Manifest:** Claude finds `SM_Teacup`, `SM_KitchenTable`, and `GrabComponent`.
3.  **Execute MCP Calls:**
    *   `SpawnActor(Asset="SM_Teacup", Location=GetTopSurface("SM_KitchenTable"))`
    *   `AddComponent(Actor="SM_Teacup", Component="GrabComponent")`

**Example 2: Script Analysis - *Ordinary Courage VR***
Based on the provided script for the *Ordinary Courage VR* experience, here is exactly what Claude + MCP can automate, and what it cannot.

#### What Claude + MCP CAN Automate (~70% of the project)
The LLM can handle all the "If X happens, do Y" structural wiring.

| Script Interaction | How Claude Wires It via MCP |
| :--- | :--- |
| **Navigation Markers** | Spawns a trigger volume at the specified location. Binds an `OnOverlap` event to a function that disables the VR pawn's teleportation component. |
| **Object Grabbing (Door Knob, Phone)** | Finds the specified actor. Attaches a `GrabComponent` to it. Binds the `OnGrab` event to trigger the next sequence (e.g., play door animation, open UI widget). |
| **Text Message Sequence** | Creates a UI widget. Sets the predetermined text strings from the script. Binds the VR controller's trigger button to an array index incrementer to progress the text. |
| **Matching Objects to Photos** | Binds an `OnGrab` event to the object (e.g., the Teapot). Adds a proximity check (distance to the photo's location marker). On success, spawns the corresponding character (`SK_Heather_Child`) and triggers the VO audio file. |
| **Scene Transitions** | Binds the completion of the final interaction in a room to a level streaming command or a post-process fade-to-black material parameter. |

#### What Claude + MCP CANNOT Automate (The Creative 30%)
The LLM cannot generate or evaluate the quality of creative, spatial, or emotional data.

*   **Character Animation:** The script calls for Heather to twirl, run, and hug Susan's leg. Claude cannot generate this animation. It must be hand-animated or mocapped and saved as an asset. Claude can only *trigger* it to play.
*   **Emotional Performance:** The Detective's slumped posture and Heather's watery eyes require an animator's touch.
*   **Custom Visual Effects:** The abstract sequence of shapes pouring out of a computer, or the walls warping in the hospital hallway, require a Technical Artist to build a custom Niagara system or shader. Claude can turn the shader on, but it cannot write the shader math.
*   **Sound Design:** The visceral "ungodly wail" and the specific ambient mixing must be created by a sound designer.

### 4. The Final Pipeline

1.  **Human/Artists:** Create the 3D assets, animations, VFX, and audio.
2.  **System:** Adaptively quantizes the asset metadata into a lightweight manifest.
3.  **Human:** Writes the script (the intent).
4.  **Claude (LLM):** Reads the script, cross-references the manifest, and determines the logical connections.
5.  **MCP Server:** Translates Claude's logic into Unreal Engine API calls, spawning actors, attaching components, and binding events.
6.  **Human:** Reviews the generated level, adjusts timing, and polishes the creative feel.

This architecture proves that you do not need a massive Generative AI model to build a game. You only need a lightweight LLM to act as a logic router, connecting perfectly quantized, human-made assets together.
