# Lambda Signature Mismatch Fix

## Problem

`FRequestHandler` is defined as:

```cpp
using FRequestHandler = TFunction<FString(const TMap<FString, FString>&, const FString&)>;
```

This requires two parameters: `Params` (query parameters) and `Body` (POST body).

135 handler entries in `AgenticMCPServer.cpp` used a single-param lambda:

```cpp
// WRONG - single param
HandlerMap.Add(TEXT("bpCreateBlueprint"), [this](const FString& Body) {
    return HandleBPCreateBlueprint(Body);
});
```

This caused a lambda signature mismatch compile error because the `TFunction` expects `(const TMap<FString, FString>&, const FString&)` but the lambda only accepted `(const FString&)`.

## Fix

Updated all 135 single-param lambdas to accept both parameters:

```cpp
// CORRECT - two params, handler still only receives Body
HandlerMap.Add(TEXT("bpCreateBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body) {
    return HandleBPCreateBlueprint(Body);
});
```

The handler function declarations remain unchanged. They still accept only `Body`. The lambda captures `Params` to satisfy the `FRequestHandler` signature but does not forward it to handlers that do not use query parameters.

65 handlers that already accepted both `(Params, Body)` were not affected.

## Files Changed

- `Source/AgenticMCP/Private/AgenticMCPServer.cpp` -- 135 lambda signatures updated

## Verification

- 0 single-param lambdas remain
- 396 total HandlerMap entries, all using two-param signature
- Invocation at line 2680: `(*Handler)(Req->QueryParams, Req->Body)` matches the typedef
