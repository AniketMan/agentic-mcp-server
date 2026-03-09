"""
security.py — Security utilities for the UE Level Editor.

Provides path validation, input sanitization, and other security helpers
to prevent path traversal, injection, and other attacks.
"""

import os
import re
import logging
from pathlib import Path
from typing import Optional, Union

logger = logging.getLogger(__name__)


class PathTraversalError(Exception):
    """Raised when a path traversal attempt is detected."""
    pass


class InvalidInputError(Exception):
    """Raised when user input fails validation."""
    pass


ALLOWED_ASSET_EXTENSIONS = {'.uasset', '.umap', '.uexp'}


def validate_path(
    user_path: Union[str, Path],
    allowed_base: Union[str, Path],
    must_exist: bool = True,
    allowed_extensions: Optional[set] = None,
    allow_directories: bool = False,
) -> Path:
    """
    Validate that a user-provided path is safe and within allowed boundaries.

    Args:
        user_path: The path provided by the user.
        allowed_base: The base directory that all paths must be under.
        must_exist: If True, raises an error if the path doesn't exist.
        allowed_extensions: Set of allowed file extensions (e.g., {'.umap', '.uasset'}).
        allow_directories: If True, allows directories (not just files).

    Returns:
        The resolved, validated Path object.

    Raises:
        PathTraversalError: If the path is outside allowed boundaries.
        FileNotFoundError: If must_exist=True and path doesn't exist.
        InvalidInputError: If the path has an invalid extension.
    """
    if user_path is None:
        raise InvalidInputError("Path cannot be None")

    user_path = str(user_path)

    if '..' in user_path:
        logger.warning(f"Path traversal attempt blocked: {user_path}")
        raise PathTraversalError(f"Path traversal not allowed: {user_path}")

    try:
        resolved = Path(user_path).resolve()
        base_resolved = Path(allowed_base).resolve()
    except Exception as e:
        raise InvalidInputError(f"Invalid path format: {e}")

    try:
        resolved.relative_to(base_resolved)
    except ValueError:
        logger.warning(f"Path outside allowed base: {resolved} not under {base_resolved}")
        raise PathTraversalError(
            f"Path '{resolved}' is outside allowed directory '{base_resolved}'"
        )

    if must_exist and not resolved.exists():
        raise FileNotFoundError(f"Path does not exist: {resolved}")

    if resolved.exists():
        if resolved.is_dir():
            if not allow_directories:
                raise InvalidInputError(f"Path is a directory, expected file: {resolved}")
        else:
            if allowed_extensions is not None:
                ext = resolved.suffix.lower()
                if ext not in allowed_extensions:
                    raise InvalidInputError(
                        f"File extension '{ext}' not allowed. Allowed: {allowed_extensions}"
                    )

    return resolved


def validate_output_path(
    output_path: Union[str, Path],
    allowed_base: Union[str, Path],
    allowed_extensions: Optional[set] = None,
) -> Path:
    """
    Validate an output path for writing files safely.

    Unlike validate_path, this doesn't require the file to exist,
    but it does validate the parent directory exists and is writable.
    """
    if output_path is None:
        raise InvalidInputError("Output path cannot be None")

    output_path = str(output_path)

    if '..' in output_path:
        logger.warning(f"Path traversal attempt blocked in output: {output_path}")
        raise PathTraversalError(f"Path traversal not allowed: {output_path}")

    try:
        resolved = Path(output_path).resolve()
        base_resolved = Path(allowed_base).resolve()
    except Exception as e:
        raise InvalidInputError(f"Invalid path format: {e}")

    try:
        resolved.relative_to(base_resolved)
    except ValueError:
        logger.warning(f"Output path outside allowed base: {resolved}")
        raise PathTraversalError(
            f"Output path '{resolved}' is outside allowed directory '{base_resolved}'"
        )

    if allowed_extensions is not None:
        ext = resolved.suffix.lower()
        if ext not in allowed_extensions:
            raise InvalidInputError(
                f"Output extension '{ext}' not allowed. Allowed: {allowed_extensions}"
            )

    parent = resolved.parent
    if not parent.exists():
        raise InvalidInputError(f"Parent directory does not exist: {parent}")

    return resolved


def sanitize_actor_name(name: str) -> str:
    """Sanitize an actor name to prevent injection."""
    if not name:
        raise InvalidInputError("Actor name cannot be empty")

    sanitized = re.sub(r'[^a-zA-Z0-9_\-]', '', name)

    if not sanitized:
        raise InvalidInputError(f"Actor name contains no valid characters: {name}")

    return sanitized


def sanitize_property_name(name: str) -> str:
    """Sanitize a property name."""
    if not name:
        raise InvalidInputError("Property name cannot be empty")

    sanitized = re.sub(r'[^a-zA-Z0-9_]', '', name)

    if not sanitized:
        raise InvalidInputError(f"Property name contains no valid characters: {name}")

    return sanitized
