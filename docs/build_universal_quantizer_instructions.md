# System Prompt / Instructions for Claude: Building the Universal Adaptive Quantizer

**Context:**
I am building a local, GPU-accelerated pipeline where all data (3D models, images, video, audio, and text documentation) is stored as sparse, quantized float16/int16 binary files. This allows me to use my 2TB+ NVMe drive as an "infinite context window" for a small local LLM (Llama 3.1 8B) running on an RTX 5080, bypassing the need for massive cloud models.

**Your Task:**
Write a Python application called `universal_adaptive_quantizer.py`. This tool must ingest various file formats, extract their raw numerical data, adaptively quantize it (dynamically choosing float16, int16, or int8 based on the data's complexity and semantic role), apply sparse indexing (skipping default/zero values), and write the result to a custom binary format with a schema header. It must also include a retrieval engine for text embeddings.

### Core Requirements

**1. Format Parsers (The Frontend)**
The script must recursively scan a directory and route files to the correct parser based on extension:
*   **3D Models (`.usd`, `.usdz`, `.fbx`, `.obj`, `.glb`):** Use `trimesh` or `pxr` (OpenUSD) to extract vertices, normals, and UVs as numpy arrays.
*   **Images (`.png`, `.jpg`, `.exr`, `.tiff`):** Use `Pillow` or `OpenEXR` to extract pixel data as numpy arrays.
*   **Audio (`.wav`, `.mp3`, `.flac`):** Use `soundfile` or `librosa` to extract the waveform. For TTS profiles, support chunking by phoneme/silence boundaries.
*   **Text/Docs (`.md`, `.txt`, `.html`, `.pdf`):** Use `pdfplumber` or standard I/O to read text. Chunk the text into ~500 token segments. Use `sentence-transformers` (e.g., `all-MiniLM-L6-v2`) to generate embeddings for each chunk.

**2. The Adaptive Quantization Backend**
All extracted numerical data must pass through an adaptive quantization function that determines the minimum necessary precision:
*   **Hero Assets / Complex Geometry:** Convert float32 arrays to `np.float16` (using `array.astype(np.float16)`).
*   **Background Props / Simple Geometry:** Convert to `np.int8` normalized to a bounding box to save maximum space.
*   **Audio:** Convert to `np.int16`.
*   **Sparse Indexing:** Implement a basic sparse storage mechanism. Define a default value (e.g., 0.0). Create a bitmask or index array of non-default values. Only store the non-default values in the final binary array.

**3. Binary Output Format**
Write the data to disk with custom extensions (`.qmesh`, `.qimg`, `.qaud`, `.qtext`). The binary file must have a structured header:
*   Magic bytes (e.g., `QUANT1`)
*   Data type identifier (enum for mesh, image, audio, text)
*   Original dimensions/shape
*   Default/Zero value used for sparse indexing
*   Length of sparse index
*   [Sparse Index Data]
*   [Quantized Values]

**4. Text Retrieval Engine**
For text documents, the script must also build a local vector index.
*   Use `FAISS` or a flat numpy cosine similarity search.
*   Store the chunked text strings alongside their float16 embeddings.
*   Provide a function `retrieve_context(query_string, top_k=5)` that embeds a query, searches the index, and returns the relevant text chunks.

**5. LLM Wrapper (Optional but recommended)**
Provide a simple wrapper function that takes the output of `retrieve_context`, formats it into a prompt, and sends it to a local llama.cpp endpoint (e.g., `http://localhost:8081/completion` using the Llama 3.1 8B Instruct Q4_K_M model).

### Code Style Guidelines
*   Write robust, efficient Python 3.11+ code.
*   Use `numpy` heavily for vectorized operations.
*   Include comprehensive error handling (do not crash on a single corrupted file during a batch directory scan; log the error and continue).
*   Ensure all code uses ASCII characters only.
*   Provide a clear `requirements.txt` for all necessary libraries.

**Output:**
Please provide the complete, runnable `universal_quantizer.py` script and the `requirements.txt`. Do not provide partial snippets; provide the full architecture ready-to-run tool.
