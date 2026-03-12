#!/usr/bin/env python3
"""
Universal Adaptive Quantizer
=============================
Scans a project directory, extracts numerical data from 3D models, images,
audio, and text documents, adaptively quantizes each asset based on its
semantic role, and builds a FAISS vector index for RAG retrieval.

Author: JARVIS
Date: March 2026

Usage:
    python universal_adaptive_quantizer.py scan <project_dir> [--output <output_dir>]
    python universal_adaptive_quantizer.py query "<question>" [--top_k 5]
    python universal_adaptive_quantizer.py ask "<question>" [--model qwen2.5-coder:7b]
"""

import argparse
import hashlib
import json
import logging
import os
import struct
import sys
import time
from pathlib import Path
from typing import Optional

import numpy as np

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="[%(levelname)s] %(message)s",
    stream=sys.stderr,
)
log = logging.getLogger("quantizer")

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
MAGIC_BYTES = b"QUANT1"
VERSION = 1

# Data type identifiers for the binary header
DTYPE_MESH = 0x01
DTYPE_IMAGE = 0x02
DTYPE_AUDIO = 0x03
DTYPE_TEXT = 0x04

# Precision tiers
TIER_HERO = "hero"        # float16 -- full precision
TIER_STANDARD = "standard" # int16 -- moderate precision
TIER_BACKGROUND = "background"  # int8 -- minimal precision

# File extension routing
MESH_EXTENSIONS = {".usd", ".usdz", ".fbx", ".obj", ".glb", ".gltf", ".stl", ".ply"}
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".exr", ".tiff", ".tif", ".bmp", ".hdr"}
AUDIO_EXTENSIONS = {".wav", ".mp3", ".flac", ".ogg", ".aiff"}
TEXT_EXTENSIONS = {".md", ".txt", ".html", ".pdf", ".rst", ".json", ".yaml", ".yml", ".csv"}

# UE5 asset extensions (metadata only -- we extract what we can from names/paths)
UE5_EXTENSIONS = {".uasset", ".umap"}

# Chunk size for text embedding
TEXT_CHUNK_SIZE = 500  # tokens (approx 4 chars per token)
TEXT_CHUNK_CHARS = TEXT_CHUNK_SIZE * 4

# Default output directory name
DEFAULT_OUTPUT_DIR = ".quantized"


# ---------------------------------------------------------------------------
# Precision Tier Assignment
# ---------------------------------------------------------------------------
def assign_tier(filepath: Path, file_size: int) -> str:
    """
    Assign a precision tier based on file path heuristics and size.

    Rules:
    - Files in directories containing 'hero', 'main', 'character', 'player'
      or files > 50MB -> TIER_HERO
    - Files in directories containing 'background', 'prop', 'env', 'foliage'
      or files < 1MB -> TIER_BACKGROUND
    - Everything else -> TIER_STANDARD

    These heuristics can be overridden by a config file in the future.
    """
    path_lower = str(filepath).lower()

    hero_keywords = ["hero", "main", "character", "player", "protagonist",
                     "skeletal", "sk_", "anim_"]
    bg_keywords = ["background", "prop", "env", "foliage", "decor",
                   "filler", "debris", "sm_rock", "sm_tree"]

    for kw in hero_keywords:
        if kw in path_lower:
            return TIER_HERO

    for kw in bg_keywords:
        if kw in path_lower:
            return TIER_BACKGROUND

    # Size-based fallback
    if file_size > 50 * 1024 * 1024:  # > 50MB
        return TIER_HERO
    elif file_size < 1 * 1024 * 1024:  # < 1MB
        return TIER_BACKGROUND

    return TIER_STANDARD


def tier_to_dtype(tier: str, data_type: int):
    """Map a precision tier to a numpy dtype for quantization."""
    if data_type == DTYPE_AUDIO:
        return np.int16  # Audio is always int16

    if tier == TIER_HERO:
        return np.float16
    elif tier == TIER_STANDARD:
        return np.int16
    else:  # TIER_BACKGROUND
        return np.int8


# ---------------------------------------------------------------------------
# Sparse Encoding
# ---------------------------------------------------------------------------
def sparse_encode(data: np.ndarray, default_value: float = 0.0):
    """
    Encode a flat array using sparse indexing.
    Returns (indices, values, default_value) where indices are the positions
    of non-default values.
    """
    if data.size == 0:
        return np.array([], dtype=np.uint32), data, default_value

    mask = data != default_value
    indices = np.where(mask)[0].astype(np.uint32)
    values = data[mask]

    # Only use sparse encoding if it actually saves space (>50% zeros)
    sparsity = 1.0 - (len(indices) / max(data.size, 1))
    if sparsity < 0.5:
        # Dense is more efficient -- return all indices
        return np.arange(data.size, dtype=np.uint32), data, default_value

    return indices, values, default_value


# ---------------------------------------------------------------------------
# Binary Writer
# ---------------------------------------------------------------------------
def write_quantized_binary(
    output_path: Path,
    data_type: int,
    original_shape: tuple,
    quantized_data: np.ndarray,
    tier: str,
    metadata: dict,
    default_value: float = 0.0,
):
    """
    Write a quantized binary file with structured header.

    Format:
        [6 bytes]  Magic: QUANT1
        [1 byte]   Version
        [1 byte]   Data type (mesh/image/audio/text)
        [1 byte]   Precision tier (0=hero, 1=standard, 2=background)
        [1 byte]   Number of dimensions
        [4 bytes each] Dimension sizes
        [4 bytes]  Default value (float32)
        [4 bytes]  Number of sparse indices
        [4 bytes each] Sparse indices
        [N bytes]  Quantized values
        [4 bytes]  Metadata JSON length
        [N bytes]  Metadata JSON (UTF-8)
    """
    indices, values, default_val = sparse_encode(quantized_data.flatten(), default_value)

    tier_byte = {TIER_HERO: 0, TIER_STANDARD: 1, TIER_BACKGROUND: 2}[tier]

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        # Header
        f.write(MAGIC_BYTES)
        f.write(struct.pack("B", VERSION))
        f.write(struct.pack("B", data_type))
        f.write(struct.pack("B", tier_byte))

        # Shape
        f.write(struct.pack("B", len(original_shape)))
        for dim in original_shape:
            f.write(struct.pack("<I", dim))

        # Sparse info
        f.write(struct.pack("<f", default_val))
        f.write(struct.pack("<I", len(indices)))

        # Sparse indices
        if len(indices) > 0:
            f.write(indices.tobytes())

        # Quantized values
        if len(values) > 0:
            f.write(values.tobytes())

        # Metadata
        meta_json = json.dumps(metadata, ensure_ascii=True).encode("utf-8")
        f.write(struct.pack("<I", len(meta_json)))
        f.write(meta_json)

    return output_path


# ---------------------------------------------------------------------------
# Format Parsers
# ---------------------------------------------------------------------------

def parse_mesh(filepath: Path, tier: str) -> Optional[dict]:
    """Parse a 3D mesh file and return quantized vertex/normal data."""
    try:
        import trimesh
    except ImportError:
        log.warning("trimesh not installed. Skipping mesh: %s", filepath)
        return None

    try:
        scene = trimesh.load(str(filepath), force="scene")
        all_vertices = []
        all_normals = []

        if hasattr(scene, "geometry"):
            for name, geom in scene.geometry.items():
                if hasattr(geom, "vertices"):
                    all_vertices.append(geom.vertices)
                if hasattr(geom, "vertex_normals"):
                    all_normals.append(geom.vertex_normals)
        elif hasattr(scene, "vertices"):
            all_vertices.append(scene.vertices)
            if hasattr(scene, "vertex_normals"):
                all_normals.append(scene.vertex_normals)

        if not all_vertices:
            log.warning("No vertices found in mesh: %s", filepath)
            return None

        vertices = np.concatenate(all_vertices, axis=0).astype(np.float32)
        normals = np.concatenate(all_normals, axis=0).astype(np.float32) if all_normals else np.array([])

        target_dtype = tier_to_dtype(tier, DTYPE_MESH)

        if target_dtype == np.int8:
            # Normalize to bounding box and quantize to int8
            bbox_min = vertices.min(axis=0)
            bbox_max = vertices.max(axis=0)
            bbox_range = bbox_max - bbox_min
            bbox_range[bbox_range == 0] = 1.0  # Prevent division by zero
            normalized = (vertices - bbox_min) / bbox_range
            q_vertices = (normalized * 254 - 127).astype(np.int8)
        elif target_dtype == np.int16:
            bbox_min = vertices.min(axis=0)
            bbox_max = vertices.max(axis=0)
            bbox_range = bbox_max - bbox_min
            bbox_range[bbox_range == 0] = 1.0
            normalized = (vertices - bbox_min) / bbox_range
            q_vertices = (normalized * 65534 - 32767).astype(np.int16)
        else:
            q_vertices = vertices.astype(np.float16)

        metadata = {
            "source": str(filepath),
            "tier": tier,
            "vertex_count": len(vertices),
            "normal_count": len(normals),
            "bbox_min": vertices.min(axis=0).tolist() if len(vertices) > 0 else [],
            "bbox_max": vertices.max(axis=0).tolist() if len(vertices) > 0 else [],
            "dtype": str(target_dtype),
        }

        return {
            "data": q_vertices,
            "shape": vertices.shape,
            "data_type": DTYPE_MESH,
            "metadata": metadata,
            "tier": tier,
            "extension": ".qmesh",
        }

    except Exception as e:
        log.error("Failed to parse mesh %s: %s", filepath, e)
        return None


def parse_image(filepath: Path, tier: str) -> Optional[dict]:
    """Parse an image file and return quantized pixel data."""
    try:
        from PIL import Image
    except ImportError:
        log.warning("Pillow not installed. Skipping image: %s", filepath)
        return None

    try:
        img = Image.open(str(filepath))
        pixels = np.array(img, dtype=np.float32)

        if pixels.ndim == 2:
            pixels = pixels[:, :, np.newaxis]

        target_dtype = tier_to_dtype(tier, DTYPE_IMAGE)

        if target_dtype == np.int8:
            q_pixels = (pixels / 255.0 * 254 - 127).astype(np.int8)
        elif target_dtype == np.int16:
            q_pixels = (pixels / 255.0 * 65534 - 32767).astype(np.int16)
        else:
            q_pixels = (pixels / 255.0).astype(np.float16)

        metadata = {
            "source": str(filepath),
            "tier": tier,
            "width": img.width,
            "height": img.height,
            "channels": pixels.shape[2] if pixels.ndim == 3 else 1,
            "mode": img.mode,
            "dtype": str(target_dtype),
        }

        return {
            "data": q_pixels,
            "shape": pixels.shape,
            "data_type": DTYPE_IMAGE,
            "metadata": metadata,
            "tier": tier,
            "extension": ".qimg",
        }

    except Exception as e:
        log.error("Failed to parse image %s: %s", filepath, e)
        return None


def parse_audio(filepath: Path, tier: str) -> Optional[dict]:
    """Parse an audio file and return quantized waveform data."""
    try:
        import soundfile as sf
    except ImportError:
        log.warning("soundfile not installed. Skipping audio: %s", filepath)
        return None

    try:
        data, samplerate = sf.read(str(filepath), dtype="float32")

        if data.ndim == 1:
            data = data[:, np.newaxis]

        # Audio is always int16
        q_audio = (data * 32767).clip(-32768, 32767).astype(np.int16)

        metadata = {
            "source": str(filepath),
            "tier": tier,
            "samplerate": samplerate,
            "channels": data.shape[1],
            "duration_seconds": round(len(data) / samplerate, 3),
            "samples": len(data),
            "dtype": "int16",
        }

        return {
            "data": q_audio,
            "shape": data.shape,
            "data_type": DTYPE_AUDIO,
            "metadata": metadata,
            "tier": tier,
            "extension": ".qaud",
        }

    except Exception as e:
        log.error("Failed to parse audio %s: %s", filepath, e)
        return None


def parse_text(filepath: Path) -> Optional[list]:
    """
    Parse a text document and return chunked text with embeddings.
    Returns a list of dicts: [{text, embedding, source, chunk_index}, ...]
    """
    try:
        text = ""
        ext = filepath.suffix.lower()

        if ext == ".pdf":
            try:
                import pdfplumber
                with pdfplumber.open(str(filepath)) as pdf:
                    for page in pdf.pages:
                        page_text = page.extract_text()
                        if page_text:
                            text += page_text + "\n"
            except ImportError:
                log.warning("pdfplumber not installed. Skipping PDF: %s", filepath)
                return None
        else:
            text = filepath.read_text(encoding="utf-8", errors="replace")

        if not text.strip():
            return None

        # Chunk the text
        chunks = []
        for i in range(0, len(text), TEXT_CHUNK_CHARS):
            chunk = text[i:i + TEXT_CHUNK_CHARS].strip()
            if chunk:
                chunks.append({
                    "text": chunk,
                    "source": str(filepath),
                    "chunk_index": len(chunks),
                })

        return chunks if chunks else None

    except Exception as e:
        log.error("Failed to parse text %s: %s", filepath, e)
        return None


def parse_ue5_asset(filepath: Path, tier: str) -> Optional[dict]:
    """
    Parse UE5 asset metadata from the filename and path.
    We cannot read .uasset binary format without UAssetAPI, but we can
    extract useful metadata from the path structure.
    """
    name = filepath.stem
    parent = filepath.parent.name
    grandparent = filepath.parent.parent.name if filepath.parent.parent else ""

    # Infer asset type from naming conventions
    asset_type = "unknown"
    if name.startswith("SM_"):
        asset_type = "StaticMesh"
    elif name.startswith("SK_"):
        asset_type = "SkeletalMesh"
    elif name.startswith("M_") or name.startswith("MI_"):
        asset_type = "Material"
    elif name.startswith("T_"):
        asset_type = "Texture"
    elif name.startswith("BP_"):
        asset_type = "Blueprint"
    elif name.startswith("ABP_"):
        asset_type = "AnimBlueprint"
    elif name.startswith("A_") or name.startswith("AM_"):
        asset_type = "Animation"
    elif name.startswith("WBP_"):
        asset_type = "Widget"
    elif name.startswith("DA_"):
        asset_type = "DataAsset"
    elif name.startswith("DT_"):
        asset_type = "DataTable"
    elif name.startswith("NS_") or name.startswith("NE_"):
        asset_type = "Niagara"
    elif name.startswith("S_") or name.startswith("SB_"):
        asset_type = "Sound"
    elif name.startswith("LS_"):
        asset_type = "LevelSequence"
    elif filepath.suffix == ".umap":
        asset_type = "Level"

    metadata = {
        "source": str(filepath),
        "tier": tier,
        "asset_name": name,
        "asset_type": asset_type,
        "parent_folder": parent,
        "grandparent_folder": grandparent,
        "content_path": "/Game/" + str(filepath).split("Content/")[-1].replace("\\", "/").rsplit(".", 1)[0]
            if "Content/" in str(filepath) or "Content\\" in str(filepath) else str(filepath),
    }

    # Create a text representation for embedding
    text_repr = (
        f"Asset: {name}\n"
        f"Type: {asset_type}\n"
        f"Path: {metadata['content_path']}\n"
        f"Folder: {parent}/{grandparent}\n"
        f"Tier: {tier}\n"
    )

    return {
        "text": text_repr,
        "metadata": metadata,
        "source": str(filepath),
    }


# ---------------------------------------------------------------------------
# Embedding Engine
# ---------------------------------------------------------------------------
class EmbeddingEngine:
    """Manages text embeddings using sentence-transformers and FAISS."""

    def __init__(self, index_dir: Path):
        self.index_dir = index_dir
        self.index_dir.mkdir(parents=True, exist_ok=True)
        self.model = None
        self.index = None
        self.chunks = []  # List of {text, source, chunk_index, metadata}
        self.dimension = 384  # all-MiniLM-L6-v2 output dimension

    def _load_model(self):
        """Lazy-load the sentence transformer model."""
        if self.model is not None:
            return

        try:
            from sentence_transformers import SentenceTransformer
            log.info("Loading embedding model (all-MiniLM-L6-v2)...")
            self.model = SentenceTransformer("all-MiniLM-L6-v2")
            self.dimension = self.model.get_sentence_embedding_dimension()
            log.info("Embedding model loaded. Dimension: %d", self.dimension)
        except ImportError:
            log.error("sentence-transformers not installed. Text retrieval disabled.")
            raise

    def _init_index(self):
        """Initialize or load the FAISS index."""
        try:
            import faiss
        except ImportError:
            log.error("faiss-cpu not installed. Text retrieval disabled.")
            raise

        index_path = self.index_dir / "faiss.index"
        chunks_path = self.index_dir / "chunks.json"

        if index_path.exists() and chunks_path.exists():
            log.info("Loading existing FAISS index from %s", self.index_dir)
            self.index = faiss.read_index(str(index_path))
            with open(chunks_path, "r", encoding="utf-8") as f:
                self.chunks = json.load(f)
            log.info("Loaded %d chunks from existing index.", len(self.chunks))
        else:
            self.index = faiss.IndexFlatIP(self.dimension)  # Inner product (cosine after normalization)
            self.chunks = []

    def add_chunks(self, chunks: list):
        """
        Add text chunks to the index.
        Each chunk is a dict with at least 'text' and 'source' keys.
        """
        self._load_model()
        if self.index is None:
            self._init_index()

        if not chunks:
            return

        texts = [c["text"] for c in chunks]
        embeddings = self.model.encode(texts, show_progress_bar=False, normalize_embeddings=True)
        embeddings = embeddings.astype(np.float32)

        self.index.add(embeddings)
        self.chunks.extend(chunks)

    def save(self):
        """Save the FAISS index and chunk metadata to disk."""
        if self.index is None:
            return

        try:
            import faiss
        except ImportError:
            return

        index_path = self.index_dir / "faiss.index"
        chunks_path = self.index_dir / "chunks.json"

        faiss.write_index(self.index, str(index_path))
        with open(chunks_path, "w", encoding="utf-8") as f:
            json.dump(self.chunks, f, ensure_ascii=True, indent=2)

        log.info("Saved FAISS index (%d vectors) to %s", self.index.ntotal, self.index_dir)

    def retrieve_context(self, query: str, top_k: int = 5) -> list:
        """
        Retrieve the top-K most relevant text chunks for a query.
        Returns list of {text, source, score, chunk_index}.
        """
        self._load_model()
        if self.index is None:
            self._init_index()

        if self.index.ntotal == 0:
            log.warning("Index is empty. Run 'scan' first.")
            return []

        query_embedding = self.model.encode([query], normalize_embeddings=True).astype(np.float32)
        scores, indices = self.index.search(query_embedding, min(top_k, self.index.ntotal))

        results = []
        for score, idx in zip(scores[0], indices[0]):
            if idx < 0 or idx >= len(self.chunks):
                continue
            chunk = self.chunks[idx].copy()
            chunk["score"] = float(score)
            results.append(chunk)

        return results


# ---------------------------------------------------------------------------
# Manifest Builder
# ---------------------------------------------------------------------------
class ManifestBuilder:
    """Builds and maintains the project manifest -- the quantized index of all assets."""

    def __init__(self, output_dir: Path):
        self.output_dir = output_dir
        self.manifest_path = output_dir / "manifest.json"
        self.manifest = {
            "version": VERSION,
            "created": time.strftime("%Y-%m-%d %H:%M:%S"),
            "assets": [],
            "stats": {
                "total_files": 0,
                "total_bytes_original": 0,
                "total_bytes_quantized": 0,
                "by_tier": {TIER_HERO: 0, TIER_STANDARD: 0, TIER_BACKGROUND: 0},
                "by_type": {},
                "errors": 0,
            },
        }

    def add_asset(self, entry: dict):
        """Add an asset entry to the manifest."""
        self.manifest["assets"].append(entry)
        tier = entry.get("tier", TIER_STANDARD)
        self.manifest["stats"]["by_tier"][tier] = self.manifest["stats"]["by_tier"].get(tier, 0) + 1
        asset_type = entry.get("asset_type", "unknown")
        self.manifest["stats"]["by_type"][asset_type] = self.manifest["stats"]["by_type"].get(asset_type, 0) + 1
        self.manifest["stats"]["total_files"] += 1

    def save(self):
        """Write manifest to disk."""
        self.output_dir.mkdir(parents=True, exist_ok=True)
        with open(self.manifest_path, "w", encoding="utf-8") as f:
            json.dump(self.manifest, f, ensure_ascii=True, indent=2)
        log.info("Manifest saved: %s (%d assets)", self.manifest_path, len(self.manifest["assets"]))


# ---------------------------------------------------------------------------
# Scanner
# ---------------------------------------------------------------------------
def scan_project(project_dir: str, output_dir: Optional[str] = None):
    """
    Scan a project directory, quantize all supported assets, and build
    the FAISS index for RAG retrieval.
    """
    project_path = Path(project_dir).resolve()
    if not project_path.exists():
        log.error("Project directory does not exist: %s", project_path)
        sys.exit(1)

    if output_dir:
        out_path = Path(output_dir).resolve()
    else:
        out_path = project_path / DEFAULT_OUTPUT_DIR

    out_path.mkdir(parents=True, exist_ok=True)

    log.info("=" * 60)
    log.info("  Universal Adaptive Quantizer")
    log.info("  Project: %s", project_path)
    log.info("  Output:  %s", out_path)
    log.info("=" * 60)

    manifest = ManifestBuilder(out_path)
    embedder = EmbeddingEngine(out_path / "index")

    # Collect all files
    all_files = []
    for root, dirs, files in os.walk(project_path):
        # Skip hidden directories and output directory
        dirs[:] = [d for d in dirs if not d.startswith(".") and d != DEFAULT_OUTPUT_DIR]
        for fname in files:
            fpath = Path(root) / fname
            if fpath.suffix.lower() in (
                MESH_EXTENSIONS | IMAGE_EXTENSIONS | AUDIO_EXTENSIONS |
                TEXT_EXTENSIONS | UE5_EXTENSIONS
            ):
                all_files.append(fpath)

    log.info("Found %d supported files.", len(all_files))

    # Batch text chunks for embedding
    text_chunk_batch = []

    for i, fpath in enumerate(all_files):
        ext = fpath.suffix.lower()
        file_size = fpath.stat().st_size
        tier = assign_tier(fpath, file_size)

        if (i + 1) % 100 == 0 or i == 0:
            log.info("Processing %d/%d: %s [%s]", i + 1, len(all_files), fpath.name, tier)

        # Route to correct parser
        if ext in MESH_EXTENSIONS:
            result = parse_mesh(fpath, tier)
            if result:
                rel_path = fpath.relative_to(project_path)
                out_file = out_path / "binary" / rel_path.with_suffix(result["extension"])
                write_quantized_binary(
                    out_file, result["data_type"], result["shape"],
                    result["data"], result["tier"], result["metadata"],
                )
                manifest.add_asset({
                    "source": str(fpath),
                    "quantized": str(out_file),
                    "tier": tier,
                    "asset_type": "mesh",
                    **result["metadata"],
                })
                # Also add mesh metadata as text for retrieval
                text_repr = json.dumps(result["metadata"], indent=2)
                text_chunk_batch.append({
                    "text": text_repr,
                    "source": str(fpath),
                    "chunk_index": 0,
                    "asset_type": "mesh",
                    "tier": tier,
                })

        elif ext in IMAGE_EXTENSIONS:
            result = parse_image(fpath, tier)
            if result:
                rel_path = fpath.relative_to(project_path)
                out_file = out_path / "binary" / rel_path.with_suffix(result["extension"])
                write_quantized_binary(
                    out_file, result["data_type"], result["shape"],
                    result["data"], result["tier"], result["metadata"],
                )
                manifest.add_asset({
                    "source": str(fpath),
                    "quantized": str(out_file),
                    "tier": tier,
                    "asset_type": "image",
                    **result["metadata"],
                })

        elif ext in AUDIO_EXTENSIONS:
            result = parse_audio(fpath, tier)
            if result:
                rel_path = fpath.relative_to(project_path)
                out_file = out_path / "binary" / rel_path.with_suffix(result["extension"])
                write_quantized_binary(
                    out_file, result["data_type"], result["shape"],
                    result["data"], result["tier"], result["metadata"],
                )
                manifest.add_asset({
                    "source": str(fpath),
                    "quantized": str(out_file),
                    "tier": tier,
                    "asset_type": "audio",
                    **result["metadata"],
                })

        elif ext in TEXT_EXTENSIONS:
            chunks = parse_text(fpath)
            if chunks:
                text_chunk_batch.extend(chunks)
                manifest.add_asset({
                    "source": str(fpath),
                    "tier": tier,
                    "asset_type": "text",
                    "chunks": len(chunks),
                })

        elif ext in UE5_EXTENSIONS:
            result = parse_ue5_asset(fpath, tier)
            if result:
                text_chunk_batch.append({
                    "text": result["text"],
                    "source": result["source"],
                    "chunk_index": 0,
                    "asset_type": result["metadata"]["asset_type"],
                    "tier": tier,
                    "metadata": result["metadata"],
                })
                manifest.add_asset({
                    "source": str(fpath),
                    "tier": tier,
                    **result["metadata"],
                })

    # Embed all text chunks in batches
    if text_chunk_batch:
        log.info("Embedding %d text chunks...", len(text_chunk_batch))
        batch_size = 256
        for batch_start in range(0, len(text_chunk_batch), batch_size):
            batch = text_chunk_batch[batch_start:batch_start + batch_size]
            embedder.add_chunks(batch)
            if batch_start > 0 and batch_start % 1024 == 0:
                log.info("  Embedded %d/%d chunks", batch_start, len(text_chunk_batch))

        embedder.save()

    manifest.save()

    # Print summary
    stats = manifest.manifest["stats"]
    log.info("")
    log.info("=" * 60)
    log.info("  SCAN COMPLETE")
    log.info("  Total files:  %d", stats["total_files"])
    log.info("  Errors:       %d", stats["errors"])
    log.info("  By tier:")
    for tier_name, count in stats["by_tier"].items():
        log.info("    %-12s %d", tier_name, count)
    log.info("  By type:")
    for type_name, count in stats["by_type"].items():
        log.info("    %-12s %d", type_name, count)
    log.info("  Text chunks indexed: %d", len(text_chunk_batch))
    log.info("  Output: %s", out_path)
    log.info("=" * 60)


# ---------------------------------------------------------------------------
# Query / Retrieve
# ---------------------------------------------------------------------------
def query_index(output_dir: str, query: str, top_k: int = 5):
    """Query the FAISS index and print results."""
    out_path = Path(output_dir).resolve()
    embedder = EmbeddingEngine(out_path / "index")

    results = embedder.retrieve_context(query, top_k=top_k)

    if not results:
        print("No results found. Run 'scan' first to build the index.")
        return results

    print(f"\nTop {len(results)} results for: \"{query}\"\n")
    print("-" * 60)
    for i, r in enumerate(results):
        score = r.get("score", 0.0)
        source = r.get("source", "unknown")
        text = r.get("text", "")[:200]
        tier = r.get("tier", "unknown")
        print(f"  [{i+1}] Score: {score:.4f} | Tier: {tier} | Source: {os.path.basename(source)}")
        print(f"      {text}")
        print()

    return results


# ---------------------------------------------------------------------------
# LLM Wrapper (Ollama)
# ---------------------------------------------------------------------------
def ask_llm(output_dir: str, question: str, model: str = "qwen2.5-coder:7b", top_k: int = 5):
    """
    Retrieve context from the FAISS index, format a prompt, and send
    it to the local Ollama endpoint.
    """
    import requests

    out_path = Path(output_dir).resolve()
    embedder = EmbeddingEngine(out_path / "index")

    # Retrieve relevant context
    results = embedder.retrieve_context(question, top_k=top_k)

    if not results:
        print("No context available. Run 'scan' first.")
        return

    # Build the context block with confidence metadata
    context_block = ""
    for i, r in enumerate(results):
        score = r.get("score", 0.0)
        tier = r.get("tier", "unknown")
        source = os.path.basename(r.get("source", "unknown"))
        context_block += (
            f"--- Chunk {i+1} (score: {score:.4f}, tier: {tier}, source: {source}) ---\n"
            f"{r['text']}\n\n"
        )

    # Calculate average retrieval confidence
    avg_score = sum(r.get("score", 0.0) for r in results) / max(len(results), 1)

    prompt = (
        f"You are an AI assistant with access to a quantized project manifest.\n"
        f"Retrieval confidence: {avg_score:.4f}\n"
        f"If retrieval confidence is below 0.70, state what data is missing before answering.\n\n"
        f"## Retrieved Context\n\n{context_block}\n"
        f"## Question\n\n{question}\n\n"
        f"## Answer\n\n"
    )

    print(f"Retrieval confidence: {avg_score:.4f}")
    print(f"Sending to {model} via Ollama...\n")

    try:
        response = requests.post(
            "http://localhost:11434/api/generate",
            json={
                "model": model,
                "prompt": prompt,
                "stream": True,
                "options": {
                    "temperature": 0.1,
                    "num_predict": 2048,
                },
            },
            stream=True,
            timeout=120,
        )
        response.raise_for_status()

        for line in response.iter_lines():
            if line:
                data = json.loads(line)
                token = data.get("response", "")
                print(token, end="", flush=True)
                if data.get("done", False):
                    break

        print("\n")

    except requests.ConnectionError:
        log.error("Cannot connect to Ollama at http://localhost:11434. Is it running?")
    except Exception as e:
        log.error("LLM request failed: %s", e)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Universal Adaptive Quantizer - Scan, index, and query project assets",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # scan
    scan_parser = subparsers.add_parser("scan", help="Scan and quantize a project directory")
    scan_parser.add_argument("project_dir", help="Path to the project directory")
    scan_parser.add_argument("--output", "-o", help="Output directory (default: <project_dir>/.quantized)")

    # query
    query_parser = subparsers.add_parser("query", help="Query the FAISS index")
    query_parser.add_argument("question", help="Search query")
    query_parser.add_argument("--index", "-i", required=True, help="Path to the .quantized output directory")
    query_parser.add_argument("--top_k", "-k", type=int, default=5, help="Number of results (default: 5)")

    # ask
    ask_parser = subparsers.add_parser("ask", help="Ask the LLM with RAG context")
    ask_parser.add_argument("question", help="Question for the LLM")
    ask_parser.add_argument("--index", "-i", required=True, help="Path to the .quantized output directory")
    ask_parser.add_argument("--model", "-m", default="qwen2.5-coder:7b", help="Ollama model name")
    ask_parser.add_argument("--top_k", "-k", type=int, default=5, help="Number of context chunks")

    args = parser.parse_args()

    if args.command == "scan":
        scan_project(args.project_dir, args.output)
    elif args.command == "query":
        query_index(args.index, args.question, args.top_k)
    elif args.command == "ask":
        ask_llm(args.index, args.question, args.model, args.top_k)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
