# Plan Directory (Legacy)

This directory was previously used by the external Planner to store JSON execution plans. The Planner layer has been removed.

The system now uses **direct inference** -- the local Worker model receives natural language requests and executes tool calls autonomously. No pre-computed plans are needed.

This directory is kept for backward compatibility. It is not used by the current architecture.
