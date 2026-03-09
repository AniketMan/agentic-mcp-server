"""
Unit tests for core/security.py

Tests for path validation and input sanitization functions.
"""
import os
import tempfile
import pytest
from pathlib import Path

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.security import (
    validate_path,
    validate_output_path,
    sanitize_actor_name,
    sanitize_property_name,
    PathTraversalError,
    InvalidInputError,
)


class TestValidatePath:
    """Tests for validate_path function."""

    def test_valid_absolute_path(self, tmp_path):
        """Test that valid absolute paths within allowed base pass."""
        test_file = tmp_path / "test.umap"
        test_file.write_text("test content")

        result = validate_path(str(test_file), str(tmp_path))
        assert result == test_file

    def test_valid_relative_path(self, tmp_path):
        """Test that valid relative paths are resolved correctly."""
        test_file = tmp_path / "test.umap"
        test_file.write_text("test content")

        cwd = os.getcwd()
        try:
            os.chdir(tmp_path)
            result = validate_path("test.umap", str(tmp_path))
            assert result == test_file
        finally:
            os.chdir(cwd)

    def test_path_traversal_blocked(self, tmp_path):
        """Test that path traversal attempts are blocked."""
        with pytest.raises(PathTraversalError):
            validate_path("../etc/passwd", str(tmp_path))

    def test_path_traversal_dot_dot_slash(self, tmp_path):
        """Test that various traversal patterns are blocked."""
        traversal_patterns = [
            "../secret.txt",
            "subdir/../../secret.txt",
            "..\\secret.txt",
            "subdir\\..\\..\\secret.txt",
        ]
        for pattern in traversal_patterns:
            with pytest.raises(PathTraversalError):
                validate_path(pattern, str(tmp_path))

    def test_allowed_extensions(self, tmp_path):
        """Test that extension filtering works."""
        valid_file = tmp_path / "test.umap"
        valid_file.write_text("test")

        result = validate_path(
            str(valid_file),
            str(tmp_path),
            allowed_extensions={".umap", ".uasset"}
        )
        assert result == valid_file

    def test_disallowed_extension_raises(self, tmp_path):
        """Test that disallowed extensions raise an error."""
        invalid_file = tmp_path / "test.exe"
        invalid_file.write_text("test")

        with pytest.raises(InvalidInputError):
            validate_path(
                str(invalid_file),
                str(tmp_path),
                allowed_extensions={".umap", ".uasset"}
            )

    def test_nonexistent_file_raises(self, tmp_path):
        """Test that non-existent files raise an error."""
        with pytest.raises(FileNotFoundError):
            validate_path(str(tmp_path / "nonexistent.umap"), str(tmp_path))

    def test_empty_path_raises(self):
        """Test that empty paths raise an error."""
        with pytest.raises(InvalidInputError):
            validate_path("", "/tmp")

    def test_none_path_raises(self):
        """Test that None paths raise an error."""
        with pytest.raises(InvalidInputError):
            validate_path(None, "/tmp")


class TestValidateOutputPath:
    """Tests for validate_output_path function."""

    def test_valid_output_path(self, tmp_path):
        """Test that valid output paths pass."""
        out_path = tmp_path / "output.umap"
        result = validate_output_path(str(out_path), str(tmp_path))
        assert result == out_path

    def test_output_path_traversal_blocked(self, tmp_path):
        """Test that output path traversal is blocked."""
        with pytest.raises(PathTraversalError):
            validate_output_path("../output.umap", str(tmp_path))

    def test_output_extension_filtering(self, tmp_path):
        """Test output extension filtering."""
        with pytest.raises(InvalidInputError):
            validate_output_path(
                str(tmp_path / "output.exe"),
                str(tmp_path),
                allowed_extensions={".umap"}
            )


class TestSanitizeActorName:
    """Tests for sanitize_actor_name function."""

    def test_valid_actor_name(self):
        """Test that valid actor names pass through."""
        valid_names = [
            "MyActor",
            "Actor_123",
            "BP_Character",
            "Some_Actor_Name_42",
        ]
        for name in valid_names:
            result = sanitize_actor_name(name)
            assert result == name

    def test_invalid_characters_stripped(self):
        """Test that invalid characters are stripped."""
        assert sanitize_actor_name("Actor<script>") == "Actorscript"
        assert sanitize_actor_name("Actor'Name") == "ActorName"
        assert sanitize_actor_name('Actor"Name') == "ActorName"

    def test_dangerous_characters_blocked(self):
        """Test that dangerous characters are removed."""
        assert sanitize_actor_name("Actor;DROP TABLE") == "ActorDROPTABLE"
        assert sanitize_actor_name("Actor|cmd") == "Actorcmd"

    def test_empty_name_raises(self):
        """Test that empty names raise an error."""
        with pytest.raises(InvalidInputError):
            sanitize_actor_name("")

    def test_max_length_enforced(self):
        """Test that max length is enforced."""
        long_name = "A" * 500
        result = sanitize_actor_name(long_name, max_length=100)
        assert len(result) == 100


class TestSanitizePropertyName:
    """Tests for sanitize_property_name function."""

    def test_valid_property_name(self):
        """Test that valid property names pass through."""
        valid_names = [
            "bIsEnabled",
            "ActorTransform",
            "RelativeLocation",
            "SomeProperty_123",
        ]
        for name in valid_names:
            result = sanitize_property_name(name)
            assert result == name

    def test_special_chars_stripped(self):
        """Test that special characters are stripped."""
        assert sanitize_property_name("Property<>") == "Property"
        assert sanitize_property_name("Prop'erty") == "Property"

    def test_empty_property_raises(self):
        """Test that empty property names raise an error."""
        with pytest.raises(InvalidInputError):
            sanitize_property_name("")


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
