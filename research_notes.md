# Research Notes: UE-MCP and UAssetAPI Write Operations

## Key Finding
The UE-MCP project (db-lyon/ue-mcp) uses UAssetAPI in offline mode for READ-ONLY operations.
Write operations (save_asset) are only available in LIVE mode with the editor connected.

This means our tool is going BEYOND what UE-MCP does — we are doing direct binary writes
via UAssetAPI without the editor. This is more advanced and riskier, but we've proven it works
with byte-perfect round-trips on all 6 test maps.

## Our Advantage
- We patched UAssetAPI to handle the METADATA_SERIALIZATION_OFFSET section (UE 5.5+)
- We handle PropertyTypeName construction for UE 5.4+ serialization
- We've verified byte-perfect round-trips on Oculus 5.6 branch assets
- We support add/remove exports, add/remove actors, add/remove/set properties

## Risk Areas
- K2Node/Kismet bytecode editing is NOT supported (too complex, too risky)
- We should NOT attempt to modify Blueprint graph logic via binary editing
- Property-level edits and actor-level edits are safe
- Struct properties with complex inner types need careful PTN construction
