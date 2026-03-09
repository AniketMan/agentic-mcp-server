"""
UE 5.6 Level Logic Editor — Core Module

Provides Python bindings to UAssetAPI for reading, editing, and writing
Unreal Engine 5.6 .uasset and .umap files with reference integrity validation.
"""

import logging

__version__ = "0.1.0"

# Configure module-level logging
logging.getLogger(__name__).addHandler(logging.NullHandler())


def configure_logging(level=logging.INFO, format_string=None):
    """
    Configure logging for the core module.

    Args:
        level: Logging level (default: INFO)
        format_string: Custom format string (optional)
    """
    if format_string is None:
        format_string = "[%(levelname)s] %(name)s: %(message)s"

    handler = logging.StreamHandler()
    handler.setFormatter(logging.Formatter(format_string))

    logger = logging.getLogger(__name__)
    logger.setLevel(level)
    logger.addHandler(handler)

    return logger
