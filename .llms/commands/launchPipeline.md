---
displayName: Launch Agentic Pipeline
description: Boots the full local inference stack and executes an existing plan.
placeholder: "Enter the path to the plan file (e.g., plan/SOH_EXECUTION_PLAN_v2.md)"
---

Please execute the following steps in exact order to boot the AgenticMCP pipeline and run the plan. Do not skip any steps.

### Step 1: Boot the Local Inference Stack
Use your shell access to run the startup script that boots the 4 local models (Validator, Worker, QA Auditor, Spatial Reasoner).

```bash
cd C:\Users\aniketbhatt\Desktop\LATESTDOCS\agentic-mcp-server\models
start-all.bat
```

*Wait 15 seconds for the models to load into VRAM before proceeding.*

### Step 2: Start the MCP Bridge
Use your shell access to start the Node.js gatekeeper bridge in a new background process.

```bash
cd C:\Users\aniketbhatt\Desktop\LATESTDOCS\agentic-mcp-server\Tools
start cmd /k "node index.js"
```

*Wait 5 seconds for the bridge to initialize.*

### Step 3: Verify the Target Plan
Read the plan file provided in the input: `{{input}}`
Ensure it is a valid JSON execution plan. If it's a markdown file containing a JSON block, extract just the JSON into a new file at `plan/current_execution.json`.

### Step 4: Execute the Plan
Use your shell access to launch the plan through the console runner.

```bash
cd C:\Users\aniketbhatt\Desktop\LATESTDOCS\agentic-mcp-server
node Tools/console/launch.js --plan plan/current_execution.json
```

### Step 5: Monitor Progress
The console runner will output a Job ID. Use your MCP tools to poll `check_job_status` every 30 seconds until the job is `completed` or `escalated`.

If the status is `escalated`, read the escalation report, fix the plan based on the Worker's feedback, and re-launch using `--resume-from <step_id>`.
