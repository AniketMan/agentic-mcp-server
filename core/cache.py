"""
cache.py — Caching layer for UAsset operations.

Provides LRU caching for expensive operations like parsing assets,
extracting properties, and resolving names.
"""

import threading
import time
import logging
from pathlib import Path
from typing import Dict, Any, Optional, Callable, TypeVar, Generic
from dataclasses import dataclass, field
from functools import wraps
from collections import OrderedDict

logger = logging.getLogger(__name__)

T = TypeVar('T')


@dataclass
class CacheEntry(Generic[T]):
    """A single cache entry with metadata."""
    value: T
    created_at: float
    accessed_at: float
    access_count: int = 0
    size_bytes: int = 0


class LRUCache(Generic[T]):
    """
    Thread-safe LRU cache implementation.

    Features:
    - Configurable max size (by count or memory)
    - TTL (time-to-live) support
    - Thread-safe operations
    - Hit/miss statistics

    Usage:
        cache = LRUCache[dict](max_size=100, ttl_seconds=300)
        cache.set("key", {"data": "value"})
        value = cache.get("key")  # Returns cached value or None
    """

    def __init__(
        self,
        max_size: int = 100,
        ttl_seconds: Optional[float] = None,
        max_memory_bytes: Optional[int] = None,
    ):
        self._max_size = max_size
        self._ttl_seconds = ttl_seconds
        self._max_memory_bytes = max_memory_bytes
        self._cache: OrderedDict[str, CacheEntry[T]] = OrderedDict()
        self._lock = threading.RLock()
        self._hits = 0
        self._misses = 0

    def get(self, key: str) -> Optional[T]:
        """Get a value from the cache. Returns None if not found or expired."""
        with self._lock:
            if key not in self._cache:
                self._misses += 1
                return None

            entry = self._cache[key]

            # Check TTL
            if self._ttl_seconds is not None:
                age = time.time() - entry.created_at
                if age > self._ttl_seconds:
                    del self._cache[key]
                    self._misses += 1
                    return None

            # Move to end (most recently used)
            self._cache.move_to_end(key)
            entry.accessed_at = time.time()
            entry.access_count += 1
            self._hits += 1

            return entry.value

    def set(self, key: str, value: T, size_bytes: int = 0) -> None:
        """Set a value in the cache."""
        with self._lock:
            now = time.time()

            if key in self._cache:
                # Update existing entry
                self._cache[key] = CacheEntry(
                    value=value,
                    created_at=now,
                    accessed_at=now,
                    size_bytes=size_bytes,
                )
                self._cache.move_to_end(key)
            else:
                # Add new entry
                self._cache[key] = CacheEntry(
                    value=value,
                    created_at=now,
                    accessed_at=now,
                    size_bytes=size_bytes,
                )

            # Evict if over max size
            while len(self._cache) > self._max_size:
                self._cache.popitem(last=False)

            # Evict if over memory limit
            if self._max_memory_bytes is not None:
                self._evict_by_memory()

    def _evict_by_memory(self) -> None:
        """Evict entries until under memory limit."""
        total_bytes = sum(e.size_bytes for e in self._cache.values())
        while total_bytes > self._max_memory_bytes and self._cache:
            _, entry = self._cache.popitem(last=False)
            total_bytes -= entry.size_bytes

    def invalidate(self, key: str) -> bool:
        """Remove a specific key from cache. Returns True if key existed."""
        with self._lock:
            if key in self._cache:
                del self._cache[key]
                return True
            return False

    def invalidate_pattern(self, pattern: str) -> int:
        """Remove all keys matching a pattern (prefix match). Returns count removed."""
        with self._lock:
            keys_to_remove = [k for k in self._cache if k.startswith(pattern)]
            for key in keys_to_remove:
                del self._cache[key]
            return len(keys_to_remove)

    def clear(self) -> None:
        """Clear all entries from the cache."""
        with self._lock:
            self._cache.clear()
            self._hits = 0
            self._misses = 0

    @property
    def stats(self) -> Dict[str, Any]:
        """Get cache statistics."""
        with self._lock:
            total = self._hits + self._misses
            return {
                "size": len(self._cache),
                "max_size": self._max_size,
                "hits": self._hits,
                "misses": self._misses,
                "hit_rate": self._hits / total if total > 0 else 0.0,
                "memory_bytes": sum(e.size_bytes for e in self._cache.values()),
            }

    def __len__(self) -> int:
        return len(self._cache)

    def __contains__(self, key: str) -> bool:
        with self._lock:
            return key in self._cache


class AssetCache:
    """
    Specialized cache for UAsset operations.

    Caches:
    - Parsed asset files (by path + mtime)
    - Export properties (by asset path + export index)
    - Name resolutions (by asset path + name)

    Usage:
        cache = AssetCache()

        # Cache an asset
        cache.cache_asset("/path/to/level.umap", asset_file)

        # Get cached asset (checks mtime for staleness)
        cached = cache.get_asset("/path/to/level.umap")

        # Invalidate when file is modified
        cache.invalidate_asset("/path/to/level.umap")
    """

    def __init__(
        self,
        max_assets: int = 10,
        max_properties: int = 1000,
        property_ttl: float = 300.0,
    ):
        self._asset_cache: LRUCache = LRUCache(max_size=max_assets)
        self._property_cache: LRUCache = LRUCache(max_size=max_properties, ttl_seconds=property_ttl)
        self._mtime_cache: Dict[str, float] = {}
        self._lock = threading.RLock()

    def _get_mtime(self, path: Path) -> Optional[float]:
        """Get file modification time."""
        try:
            return path.stat().st_mtime
        except OSError:
            return None

    def cache_asset(self, path: str, asset) -> None:
        """Cache a parsed asset file."""
        path_obj = Path(path).resolve()
        key = str(path_obj)
        mtime = self._get_mtime(path_obj)

        with self._lock:
            self._asset_cache.set(key, asset)
            if mtime is not None:
                self._mtime_cache[key] = mtime

        logger.debug(f"Cached asset: {path}")

    def get_asset(self, path: str):
        """
        Get a cached asset if valid.

        Returns None if:
        - Not in cache
        - File has been modified since caching
        """
        path_obj = Path(path).resolve()
        key = str(path_obj)

        with self._lock:
            # Check if file was modified
            current_mtime = self._get_mtime(path_obj)
            cached_mtime = self._mtime_cache.get(key)

            if current_mtime is not None and cached_mtime is not None:
                if current_mtime > cached_mtime:
                    # File was modified, invalidate
                    self.invalidate_asset(path)
                    return None

            return self._asset_cache.get(key)

    def invalidate_asset(self, path: str) -> None:
        """Invalidate a cached asset and its related properties."""
        path_obj = Path(path).resolve()
        key = str(path_obj)

        with self._lock:
            self._asset_cache.invalidate(key)
            self._mtime_cache.pop(key, None)
            # Also invalidate all properties for this asset
            self._property_cache.invalidate_pattern(f"{key}:")

        logger.debug(f"Invalidated asset cache: {path}")

    def cache_property(self, asset_path: str, export_index: int, prop_name: str, value: Any) -> None:
        """Cache a property value."""
        key = f"{Path(asset_path).resolve()}:{export_index}:{prop_name}"
        self._property_cache.set(key, value)

    def get_property(self, asset_path: str, export_index: int, prop_name: str) -> Optional[Any]:
        """Get a cached property value."""
        key = f"{Path(asset_path).resolve()}:{export_index}:{prop_name}"
        return self._property_cache.get(key)

    def clear(self) -> None:
        """Clear all caches."""
        with self._lock:
            self._asset_cache.clear()
            self._property_cache.clear()
            self._mtime_cache.clear()

    @property
    def stats(self) -> Dict[str, Any]:
        """Get cache statistics."""
        return {
            "assets": self._asset_cache.stats,
            "properties": self._property_cache.stats,
        }


# Global cache instance
_global_cache: Optional[AssetCache] = None


def get_cache() -> AssetCache:
    """Get the global cache instance."""
    global _global_cache
    if _global_cache is None:
        _global_cache = AssetCache()
    return _global_cache


def cached_property(ttl_seconds: Optional[float] = None):
    """
    Decorator for caching method results.

    Usage:
        class MyClass:
            @cached_property(ttl_seconds=60)
            def expensive_operation(self, arg1, arg2):
                ...
    """
    def decorator(func: Callable) -> Callable:
        cache = LRUCache(max_size=100, ttl_seconds=ttl_seconds)

        @wraps(func)
        def wrapper(*args, **kwargs):
            # Create cache key from arguments
            key = f"{func.__name__}:{hash((args, tuple(sorted(kwargs.items()))))}"

            result = cache.get(key)
            if result is not None:
                return result

            result = func(*args, **kwargs)
            cache.set(key, result)
            return result

        wrapper.cache = cache
        wrapper.clear_cache = cache.clear
        return wrapper

    return decorator
