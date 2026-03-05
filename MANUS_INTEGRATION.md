> **Version 4: Ground Truth Alignment**

# Manus Agent Integration Guide: UE 5.6 Level Editor API

**Audience:** This document is for the Manus agent connected to the Perforce server. It describes the **only correct workflow** for using the **Level Editor API** to modify the SOH_VR project.

---

## 1. The Core Principle: Headless Binary Editing

The entire purpose of this toolchain is to **programmatically and safely edit Unreal Engine 5.6 assets *without* needing to open the Unreal Editor application.**

This is achieved by directly reading and writing the binary `.uasset` and `.umap` files using the **UAssetAPI** library. All operations occur in memory on the Level Editor API server and are validated before being saved.

**There is no script generation. There is no `-ExecutePythonScript`. The Unreal Editor is never opened.**

---

## 2. The Unbreakable Rules

1.  **NEVER MODIFY FILES DIRECTLY.** You do not edit `.uasset` files. You call the Level Editor API, which returns a modified binary file to you.

2.  **NEVER PUSH SCRIPTS.** There are no scripts.

3.  **NEVER ACT WITHOUT APPROVAL.** You must ask for and receive explicit user approval before submitting any files to Perforce.

---

## 3. The Correct Workflow

This is the only approved sequence of operations.

### **Phase 1: Inspection**

1.  **Pull Latest from Perforce:** Sync the `SOH_VR` workspace.
    ```bash
    p4 sync //depot/SOH_VR/...
    ```

2.  **Call the Level Editor API to Load an Asset:** Send the asset file from your local workspace to the Level Editor API.
    ```bash
    curl -X POST http://<level-editor-api-host>:8080/api/load \
      -H "Content-Type: application/json" \
      -d '{"filepath": "/path/to/your/p4/workspace/SOH_VR/Content/Maps/SL_Trailer_Logic.umap"}'
    ```

3.  **Call the Level Editor API to Inspect:** Use `GET` requests to the API to understand the asset.
    -   `GET /api/actors`
    -   `GET /api/exports`
    -   `GET /api/imports`

### **Phase 2: Modification**

4.  **Call the Write Endpoints:** Use `POST` requests to the `/api/write/*` endpoints to modify the asset in memory on the API server.
    ```bash
    # Example: Add a new property to an export
    curl -X POST http://<level-editor-api-host>:8080/api/write/add-property \
      -H "Content-Type: application/json" \
      -d '{
            "export_index": 5,
            "property_name": "bIsCool",
            "property_type": "bool",
            "value": true
          }'
    ```

5.  **Validate Changes:** Periodically call the validation endpoint to check for issues.
    ```bash
    curl http://<level-editor-api-host>:8080/api/write/validate
    ```

### **Phase 3: Save and Submit**

6.  **Call the Save Endpoint:** Once all edits are complete, call the save endpoint. This will return the full, modified binary `.uasset` file in the response body.
    ```bash
    # This returns the binary file data
    curl -X POST http://<level-editor-api-host>:8080/api/write/save \
      -H "Content-Type: application/json" \
      -d '{"validate": true, "backup": false}' \
      --output /path/to/your/p4/workspace/SOH_VR/Content/Maps/SL_Trailer_Logic.umap
    ```

7.  **STOP. ASK FOR USER APPROVAL.** Report the results to the user. Explain what changes were made. Ask for permission to submit the modified `.uasset` or `.umap` file to Perforce.

8.  **Submit the Changed ASSET to Perforce:** After getting a "yes", check out and submit the `.uasset` or `.umap` file that the API just returned.
    ```bash
    p4 edit //depot/SOH_VR/Content/Maps/SL_Trailer_Logic.umap
    p4 submit -d "[JARVIS] Added bIsCool property as per user request."
    ```

---

## 4. The `JarvisEditor` C++ Plugin and Scripting Endpoints

-   The `ue_plugin/` directory and the `/api/script/*` endpoints are for a **separate, local-only workflow** where an AI interacts with a running Unreal Editor.
-   **You will never use them.** They are irrelevant to the Manus workflow.
-   Refer to `GROUND_TRUTH.md` for the full architectural breakdown offficial architecture diagram of the two separate architectures.
