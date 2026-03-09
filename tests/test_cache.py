"""
Unit tests for core/cache.py

Tests for LRU caching and asset cache functionality.
"""
import os
import time
import pytest
from unittest.mock import Mock, patch

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.cache import LRUCache, AssetCache, get_cache, cached_property


class TestLRUCache:
    """Tests for LRUCache class."""

    def test_basic_get_set(self):
        """Test basic get and set operations."""
        cache = LRUCache(max_size=10)
        cache.set("key1", "value1")
        assert cache.get("key1") == "value1"

    def test_get_missing_key_returns_none(self):
        """Test that getting a missing key returns None."""
        cache = LRUCache(max_size=10)
        assert cache.get("nonexistent") is None

    def test_get_missing_key_returns_default(self):
        """Test that getting a missing key returns default value."""
        cache = LRUCache(max_size=10)
        assert cache.get("nonexistent", "default") == "default"

    def test_eviction_on_max_size(self):
        """Test that oldest entries are evicted when max size is reached."""
        cache = LRUCache(max_size=3)
        cache.set("a", 1)
        cache.set("b", 2)
        cache.set("c", 3)
        cache.set("d", 4)  # Should evict 'a'

        assert cache.get("a") is None
        assert cache.get("b") == 2
        assert cache.get("c") == 3
        assert cache.get("d") == 4

    def test_lru_order_on_access(self):
        """Test that accessed items move to end of LRU order."""
        cache = LRUCache(max_size=3)
        cache.set("a", 1)
        cache.set("b", 2)
        cache.set("c", 3)

        cache.get("a")  # Access 'a', making it most recently used
        cache.set("d", 4)  # Should evict 'b' (least recently used)

        assert cache.get("a") == 1
        assert cache.get("b") is None
        assert cache.get("c") == 3
        assert cache.get("d") == 4

    def test_delete_existing_key(self):
        """Test deleting an existing key."""
        cache = LRUCache(max_size=10)
        cache.set("key", "value")
        assert cache.delete("key") is True
        assert cache.get("key") is None

    def test_delete_missing_key(self):
        """Test deleting a missing key returns False."""
        cache = LRUCache(max_size=10)
        assert cache.delete("nonexistent") is False

    def test_clear(self):
        """Test clearing the cache."""
        cache = LRUCache(max_size=10)
        cache.set("a", 1)
        cache.set("b", 2)
        cache.clear()

        assert cache.get("a") is None
        assert cache.get("b") is None
        assert len(cache) == 0

    def test_len(self):
        """Test length of cache."""
        cache = LRUCache(max_size=10)
        assert len(cache) == 0
        cache.set("a", 1)
        assert len(cache) == 1
        cache.set("b", 2)
        assert len(cache) == 2

    def test_contains(self):
        """Test __contains__ method."""
        cache = LRUCache(max_size=10)
        cache.set("key", "value")
        assert "key" in cache
        assert "other" not in cache

    def test_ttl_expiration(self):
        """Test that entries expire after TTL."""
        cache = LRUCache(max_size=10, ttl_seconds=0.1)
        cache.set("key", "value")

        assert cache.get("key") == "value"
        time.sleep(0.15)
        assert cache.get("key") is None

    def test_no_ttl(self):
        """Test cache without TTL."""
        cache = LRUCache(max_size=10, ttl_seconds=None)
        cache.set("key", "value")
        time.sleep(0.1)
        assert cache.get("key") == "value"


class TestAssetCache:
    """Tests for AssetCache class."""

    def test_cache_and_get_asset(self):
        """Test caching and retrieving an asset."""
        cache = AssetCache(max_assets=10)
        mock_asset = Mock()

        cache.cache_asset("/path/to/asset.umap", mock_asset)
        result = cache.get_asset("/path/to/asset.umap")

        assert result is mock_asset

    def test_invalidate_asset(self):
        """Test invalidating a cached asset."""
        cache = AssetCache(max_assets=10)
        mock_asset = Mock()

        cache.cache_asset("/path/to/asset.umap", mock_asset)
        cache.invalidate_asset("/path/to/asset.umap")

        assert cache.get_asset("/path/to/asset.umap") is None

    def test_cache_and_get_property(self):
        """Test caching and retrieving a property."""
        cache = AssetCache(max_assets=10)

        cache.cache_property("/path/to/asset.umap", 5, "Location", {"X": 100})
        result = cache.get_property("/path/to/asset.umap", 5, "Location")

        assert result == {"X": 100}

    def test_invalidate_asset_clears_properties(self):
        """Test that invalidating an asset clears its properties."""
        cache = AssetCache(max_assets=10)

        cache.cache_property("/path/to/asset.umap", 5, "Location", {"X": 100})
        cache.invalidate_asset("/path/to/asset.umap")

        assert cache.get_property("/path/to/asset.umap", 5, "Location") is None

    def test_get_stats(self):
        """Test getting cache statistics."""
        cache = AssetCache(max_assets=10)
        mock_asset = Mock()

        cache.cache_asset("/path/to/asset1.umap", mock_asset)
        cache.cache_asset("/path/to/asset2.umap", mock_asset)

        stats = cache.get_stats()
        assert stats["cached_assets"] == 2

    def test_clear(self):
        """Test clearing the asset cache."""
        cache = AssetCache(max_assets=10)
        cache.cache_asset("/path/to/asset.umap", Mock())
        cache.cache_property("/path/to/asset.umap", 5, "Location", {"X": 100})

        cache.clear()

        assert cache.get_asset("/path/to/asset.umap") is None
        assert cache.get_property("/path/to/asset.umap", 5, "Location") is None


class TestGetCache:
    """Tests for get_cache function."""

    def test_returns_singleton(self):
        """Test that get_cache returns a singleton instance."""
        cache1 = get_cache()
        cache2 = get_cache()
        assert cache1 is cache2

    def test_returns_asset_cache(self):
        """Test that get_cache returns an AssetCache instance."""
        cache = get_cache()
        assert isinstance(cache, AssetCache)


class TestCachedPropertyDecorator:
    """Tests for cached_property decorator."""

    def test_caches_result(self):
        """Test that decorator caches the result."""
        call_count = 0

        @cached_property(ttl_seconds=60)
        def expensive_function(path):
            nonlocal call_count
            call_count += 1
            return f"result_for_{path}"

        result1 = expensive_function("/path/to/asset")
        result2 = expensive_function("/path/to/asset")

        assert result1 == result2 == "result_for_/path/to/asset"
        assert call_count == 1

    def test_different_args_not_cached(self):
        """Test that different arguments are not cached together."""
        @cached_property(ttl_seconds=60)
        def expensive_function(path):
            return f"result_for_{path}"

        result1 = expensive_function("/path/one")
        result2 = expensive_function("/path/two")

        assert result1 == "result_for_/path/one"
        assert result2 == "result_for_/path/two"

    def test_respects_ttl(self):
        """Test that decorator respects TTL."""
        call_count = 0

        @cached_property(ttl_seconds=0.1)
        def expensive_function(path):
            nonlocal call_count
            call_count += 1
            return f"result_{call_count}"

        result1 = expensive_function("/path")
        time.sleep(0.15)
        result2 = expensive_function("/path")

        assert result1 == "result_1"
        assert result2 == "result_2"
        assert call_count == 2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
