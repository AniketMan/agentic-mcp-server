# INSTRUCTION MANUAL: Programming with Adaptive Quantized Spatial Data
## A Guide for AI Agents and Developers Building High-Performance VFX/3D Software

**Author:** JARVIS
**Date:** March 2026

### 1. The Core Directive: Do Not Train Models for Deterministic Data

When tasked with building software to solve spatial, temporal, or visual problems (e.g., animation retargeting, keying, retopology, stereo conversion), your first instinct as an AI or developer might be to build a neural network. **Do not do this if the ground truth data already exists.**

*   **When to use a model:** When you need to generate *new* information that does not exist (e.g., hallucinating a background behind an object, identifying the semantic label of a pixel like SAM does).
*   **When NOT to use a model:** When you are manipulating, transferring, or blending data that you already have on a hard drive (e.g., bone transforms, pixel colors, mesh vertices).

If a user has terabytes of 3D scenes, raw footage, or simulation caches, that data *is* the model. Your job is to extract the relationships from that data, adaptively quantize them (assigning precision based on the semantic weight of the asset), and apply them.

### 2. The CPU vs. GPU Architecture Split

To build these programs efficiently, you must separate the logic from the math.

#### The CPU: Non-Quantized Logic and Orchestration
The CPU handles the high-level logic, file I/O, and data structure preparation. It does not need to be quantized.
*   **Tasks:** Parsing FBX/Alembic files, reading raw video containers, managing the scene graph, determining which bones map to which bones, and feeding data pointers to the GPU.
*   **Format:** Standard float32/float64, standard Python/C++ data structures.

#### The GPU: Quantized Math and Execution
The GPU handles the actual transfer, blending, and modification of the data.
*   **Tasks:** Matrix multiplication, pixel blending, vertex displacement, spatial proximity queries.
*   **Format:** The CPU sends the data to the GPU. The GPU immediately **quantizes it to float16 (or lower)**, performs the math, and either stores it in VRAM or streams the quantized result back to disk.

### 3. Step-by-Step Example: Building an Automated "Creature Re-conforming" Tool

**The Scenario:** A studio has 2 TB of Maya/Unreal scene files. Scene A has Actor Rig X perfectly interacting with Creature Rig Y. Scene B has a new take of Actor Rig X, and Creature Rig Y is now broken. 

**How to build the software:**

**Step 1: Data Extraction (CPU)**
*   Read Scene A. For every frame, calculate the spatial delta (distance, rotation offset) between Actor Rig X's hand and Creature Rig Y's shoulder.
*   *Do not train a model to learn how hands touch shoulders.* You have the exact mathematical offset for this specific shot.

**Step 2: Quantization and Storage (GPU)**
*   Send those deltas to the GPU.
*   Quantize the deltas to float16.
*   Store them as a lightweight binary array (e.g., a custom `.qanim` format). This takes megabytes, not gigabytes.

**Step 3: Application (GPU)**
*   Read Scene B (the new take).
*   Send the new Actor Rig X bone positions to the GPU.
*   The GPU applies the quantized deltas to the new bone positions using standard matrix addition/multiplication.
*   The GPU runs a lightweight deterministic IK solver (not AI) to ensure the creature's bones do not stretch beyond physical limits.

**Step 4: Output (CPU)**
*   The GPU sends the final, adjusted Creature Rig Y bone transforms back to the CPU to be written into the new scene file.

### 4. Hardware Implementation Guidelines

Your code must be optimized for the specific hardware architecture the user is running.

#### Apple Silicon (M1 to M5 Series)
Apple Silicon is the optimal architecture for this paradigm due to its **Unified Memory Architecture (UMA)**.
*   **The Advantage:** On a PC, moving 2 TB of data from the hard drive to RAM (CPU) to VRAM (GPU) is a massive bottleneck. On Apple Silicon, the CPU and GPU share the exact same physical memory.
*   **How to Code It:** Load the raw data into RAM using the CPU. Pass the memory pointer to the GPU using Apple's Metal Performance Shaders (MPS). The GPU reads the float32 data directly from that pointer, quantizes it to float16 in its registers, does the math, and writes it back. There is zero PCIe transfer latency.
*   **AI Assistance:** Use the Neural Engine (via CoreML) strictly for semantic tagging (e.g., running SAM to get object IDs), and feed those IDs directly into the shared memory pool for the GPU to use in its quantized math passes.

#### NVIDIA CUDA (RTX 5080 / 4080 / 3080)
NVIDIA GPUs have extreme raw compute power but suffer from the PCIe transfer bottleneck.
*   **The Advantage:** Unmatched parallel processing capability for matrix math.
*   **How to Code It (RTX 4080/5080):** These cards have massive VRAM (16GB - 32GB+). You can load large chunks of the quantized spatial graph directly into VRAM and leave it there. Write custom CUDA kernels to perform the math entirely on the card.
*   **How to Code It (RTX 3080):** With lower VRAM (10GB), you cannot load the entire dataset. You must implement aggressive streaming. Use NVIDIA DirectStorage APIs to stream quantized data directly from the NVMe SSD to the GPU VRAM, bypassing the CPU entirely for the data payload. 

### 5. Summary Checklist for AI Developers

When a user asks you to build a tool for 3D/VFX manipulation:
1.  **Does the perfect data already exist?** (Yes: use Adaptive Quantized Spatial Transfer. No: consider a Generative Model).
2.  **Is the task semantic or mathematical?** (Semantic: use an AI model like SAM to tag the data. Mathematical: use GPU compute on quantized data).
3.  **Are you storing floats as text/JSON?** (If yes, stop. Use binary float16).
4.  **Are you moving data back and forth across the PCIe bus unnecessarily?** (If yes, optimize memory pointers for UMA or use DirectStorage for CUDA).
