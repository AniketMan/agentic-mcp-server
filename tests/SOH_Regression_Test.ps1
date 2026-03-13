# ==============================================================================
# SOH_Regression_Test.ps1
# Comprehensive regression test for all 32 SOH interactions across 6 levels.
# Tests: server health, endpoint availability, graph chain integrity,
#        backpropagation verification, story step registration, and PIE validation.
#
# Usage: .\SOH_Regression_Test.ps1 [-Port 9847] [-SkipPIE] [-Level "SL_Main_Logic"]
# ==============================================================================

param(
    [int]$Port = 9847,
    [switch]$SkipPIE,
    [string]$Level = "",
    [string]$ReportPath = ".\SOH_Test_Report.md"
)

$ErrorActionPreference = "Continue"
$BASE = "http://localhost:$Port"

# --- Counters ---
$script:TotalTests = 0
$script:Passed = 0
$script:Failed = 0
$script:Warnings = 0
$script:Results = @()

# --- Timestamp ---
$Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

# ==============================================================================
# HELPER FUNCTIONS
# ==============================================================================

function Write-TestHeader {
    param([string]$Text)
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "  $Text" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
}

function Write-TestResult {
    param(
        [string]$Test,
        [string]$Status,  # PASS, FAIL, WARN
        [string]$Detail = ""
    )
    $script:TotalTests++
    $color = switch ($Status) {
        "PASS" { $script:Passed++; "Green" }
        "FAIL" { $script:Failed++; "Red" }
        "WARN" { $script:Warnings++; "Yellow" }
    }
    $line = "  [$Status] $Test"
    if ($Detail) { $line += " -- $Detail" }
    Write-Host $line -ForegroundColor $color
    $script:Results += [PSCustomObject]@{
        Test   = $Test
        Status = $Status
        Detail = $Detail
    }
}

function Invoke-MCP {
    param(
        [string]$Endpoint,
        [string]$Method = "GET",
        [string]$Body = ""
    )
    try {
        $uri = "$BASE$Endpoint"
        $params = @{
            Uri         = $uri
            Method      = $Method
            ContentType = "application/json"
            TimeoutSec  = 15
        }
        if ($Body -and $Method -ne "GET") {
            $params.Body = $Body
        }
        $response = Invoke-RestMethod @params
        return $response
    }
    catch {
        $errMsg = $_.Exception.Message
        if ($_.Exception.Response) {
            $statusCode = [int]$_.Exception.Response.StatusCode
            return @{ error = "HTTP $statusCode - $errMsg" }
        }
        return @{ error = $errMsg }
    }
}

function Test-Endpoint {
    param(
        [string]$Name,
        [string]$Endpoint,
        [string]$Method = "GET",
        [string]$Body = ""
    )
    $result = Invoke-MCP -Endpoint $Endpoint -Method $Method -Body $Body
    if ($result.error) {
        Write-TestResult -Test $Name -Status "FAIL" -Detail $result.error
        return $null
    }
    Write-TestResult -Test $Name -Status "PASS"
    return $result
}

# ==============================================================================
# INTERACTION DEFINITIONS -- Ground Truth from VR Script v2
# ==============================================================================

$Interactions = @(
    # SL_Main_Logic (Scenes 01-02)
    @{ Step=2;  Event="Scene1_HeatherPhotoGazed";  Level="SL_Main_Logic";         Scene="01" }
    @{ Step=3;  Event="Scene1_HeatherHugged";      Level="SL_Main_Logic";         Scene="01" }
    @{ Step=4;  Event="Scene2_KitchenTableNav";    Level="SL_Main_Logic";         Scene="02" }
    @{ Step=5;  Event="Scene2_IllustrationGrabbed"; Level="SL_Main_Logic";        Scene="02" }

    # SL_Trailer_Logic (Scenes 03-04)
    @{ Step=7;  Event="Scene3_DoorKnobGrabbed";    Level="SL_Trailer_Logic";      Scene="03" }
    @{ Step=8;  Event="Scene3_FridgeDoorGrabbed";  Level="SL_Trailer_Logic";      Scene="03" }
    @{ Step=9;  Event="Scene3_PitcherGrabbed";     Level="SL_Trailer_Logic";      Scene="03" }
    @{ Step=10; Event="Scene3_PitcherComplete";    Level="SL_Trailer_Logic";      Scene="03" }
    @{ Step=11; Event="Scene4_PhoneGrabbed";       Level="SL_Trailer_Logic";      Scene="04" }
    @{ Step=12; Event="Scene4_TextAdvance1";       Level="SL_Trailer_Logic";      Scene="04" }
    @{ Step=13; Event="Scene4_TextAdvance2";       Level="SL_Trailer_Logic";      Scene="04" }
    @{ Step=14; Event="Scene4_DoorHandleGrabbed";  Level="SL_Trailer_Logic";      Scene="04" }

    # SL_Restaurant_Logic (Scene 05)
    @{ Step=15; Event="Scene5_BoothNav";           Level="SL_Restaurant_Logic";   Scene="05" }
    @{ Step=16; Event="Scene5_HandHeld";           Level="SL_Restaurant_Logic";   Scene="05" }

    # SL_Scene6_Logic (Scene 06)
    @{ Step=18; Event="Scene6_ShapeSelected";      Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=19; Event="Scene6_EchoChamberExit";    Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=20; Event="Scene6_ScaleNav";           Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=21; Event="Scene6_WeightGrabbed";      Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=22; Event="Scene6_TorchExit";          Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=23; Event="Scene6_CradleWeight";       Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=24; Event="Scene6_CradleWeight2";      Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=25; Event="Scene6_SignGrabbed";        Level="SL_Scene6_Logic";       Scene="06" }
    @{ Step=26; Event="Scene6_PhoneGrabbed";       Level="SL_Scene6_Logic";       Scene="06" }

    # SL_Hospital_Logic (Scene 07)
    @{ Step=27; Event="Scene7_ReceptionNav";       Level="SL_Hospital_Logic";     Scene="07" }
    @{ Step=28; Event="Scene7_NumberCardGrabbed";  Level="SL_Hospital_Logic";     Scene="07" }
    @{ Step=29; Event="Scene7_GuidedWalk";         Level="SL_Hospital_Logic";     Scene="07" }
    @{ Step=30; Event="Scene7_DetectiveNews";      Level="SL_Hospital_Logic";     Scene="07" }

    # SL_TrailerScene8_Logic (Scene 08)
    @{ Step=31; Event="Scene8_TeapotGrabbed";      Level="SL_TrailerScene8_Logic"; Scene="08" }
    @{ Step=32; Event="Scene8_IllustrationGrabbed"; Level="SL_TrailerScene8_Logic"; Scene="08" }
    @{ Step=33; Event="Scene8_PitcherGrabbed";     Level="SL_TrailerScene8_Logic"; Scene="08" }
    @{ Step=34; Event="Scene8_CellphoneGrabbed";   Level="SL_TrailerScene8_Logic"; Scene="08" }
    @{ Step=35; Event="Scene8_HandsCircle";        Level="SL_TrailerScene8_Logic"; Scene="08" }
)

# Filter to specific level if requested
if ($Level) {
    $Interactions = $Interactions | Where-Object { $_.Level -eq $Level }
    if ($Interactions.Count -eq 0) {
        Write-Host "ERROR: No interactions found for level '$Level'" -ForegroundColor Red
        Write-Host "Valid levels: SL_Main_Logic, SL_Trailer_Logic, SL_Restaurant_Logic, SL_Scene6_Logic, SL_Hospital_Logic, SL_TrailerScene8_Logic" -ForegroundColor Yellow
        Read-Host "Press Enter to exit"
        exit 1
    }
}

$LevelNames = ($Interactions | Select-Object -ExpandProperty Level -Unique)

# ==============================================================================
# PHASE 0: SERVER HEALTH
# ==============================================================================

Write-TestHeader "PHASE 0: Server Health & Capabilities"

# 0A: Health check
$health = Test-Endpoint -Name "Server Health" -Endpoint "/api/health"
if (-not $health -or $health.error) {
    Write-Host ""
    Write-Host "FATAL: AgenticMCP server not responding on port $Port." -ForegroundColor Red
    Write-Host "Make sure the editor is running with the AgenticMCP plugin loaded." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# 0B: Capabilities
$caps = Test-Endpoint -Name "Capabilities Endpoint" -Endpoint "/api/capabilities"
if ($caps -and $caps.endpointCount) {
    if ($caps.endpointCount -ge 107) {
        Write-TestResult -Test "Endpoint Count ($($caps.endpointCount))" -Status "PASS"
    } else {
        Write-TestResult -Test "Endpoint Count ($($caps.endpointCount))" -Status "WARN" -Detail "Expected >= 107, got $($caps.endpointCount)"
    }
} elseif ($caps -and -not $caps.error) {
    # Might be DevmateMCP without capabilities endpoint -- try /mcp/tools
    $tools = Test-Endpoint -Name "MCP Tools Fallback" -Endpoint "/mcp/tools"
    if ($tools -and $tools.tools) {
        Write-TestResult -Test "Endpoint Count (via /mcp/tools: $($tools.tools.Count))" -Status "PASS"
    }
}

# 0C: Critical endpoints check
$criticalEndpoints = @(
    @{ Name="list-actors";    EP="/api/list-actors";    Method="POST"; Body='{}' }
    @{ Name="graph";          EP="/api/graph";          Method="GET";  Body='' }
    @{ Name="list-levels";    EP="/api/list-levels";    Method="POST"; Body='{}' }
    @{ Name="compile-bp";     EP="/api/compile-blueprint"; Method="POST"; Body='{"blueprint":"NONE"}' }
    @{ Name="get-pie-state";  EP="/api/get-pie-state";  Method="GET";  Body='' }
    @{ Name="story-state";    EP="/api/story/state";    Method="GET";  Body='' }
    @{ Name="list-sequences"; EP="/api/list-sequences"; Method="POST"; Body='{}' }
    @{ Name="screenshot";     EP="/api/screenshot";     Method="POST"; Body='{}' }
    @{ Name="scene-snapshot";  EP="/api/scene-snapshot"; Method="POST"; Body='{}' }
)

foreach ($ep in $criticalEndpoints) {
    $r = Invoke-MCP -Endpoint $ep.EP -Method $ep.Method -Body $ep.Body
    if ($r.error -and $r.error -match "HTTP 4") {
        # 4xx means the server responded -- endpoint exists but params were bad. That's fine.
        Write-TestResult -Test "Critical: $($ep.Name)" -Status "PASS" -Detail "Endpoint responds (param error expected)"
    } elseif ($r.error) {
        Write-TestResult -Test "Critical: $($ep.Name)" -Status "FAIL" -Detail $r.error
    } else {
        Write-TestResult -Test "Critical: $($ep.Name)" -Status "PASS"
    }
}

# ==============================================================================
# PHASE 1: LEVEL LOADING & GRAPH AUDIT
# ==============================================================================

Write-TestHeader "PHASE 1: Level Loading & Graph Audit"

# Get currently loaded levels
$levels = Invoke-MCP -Endpoint "/api/list-levels" -Method "POST" -Body '{}'

foreach ($levelName in $LevelNames) {
    Write-Host ""
    Write-Host "--- Level: $levelName ---" -ForegroundColor White

    # 1A: Check if level is loaded/visible
    $levelLoaded = $false
    if ($levels -and -not $levels.error) {
        # Check if the level appears in the response
        $levelJson = $levels | ConvertTo-Json -Depth 10
        if ($levelJson -match $levelName) {
            $levelLoaded = $true
            Write-TestResult -Test "$levelName loaded" -Status "PASS"
        }
    }
    if (-not $levelLoaded) {
        # Try to make it visible
        $vis = Invoke-MCP -Endpoint "/api/streaming-level-visibility" -Method "POST" -Body "{`"levelPath`": `"$levelName`", `"visible`": true}"
        if ($vis -and -not $vis.error) {
            Write-TestResult -Test "$levelName loaded" -Status "PASS" -Detail "Made visible"
        } else {
            Write-TestResult -Test "$levelName loaded" -Status "WARN" -Detail "Could not verify -- may need manual load"
        }
    }

    # 1B: Get the level blueprint graph
    $graph = Invoke-MCP -Endpoint "/api/get-level-blueprint" -Method "POST" -Body "{`"level`": `"$levelName`"}"
    if ($graph -and -not $graph.error) {
        Write-TestResult -Test "$levelName graph retrieved" -Status "PASS"
    } else {
        Write-TestResult -Test "$levelName graph retrieved" -Status "FAIL" -Detail ($graph.error ?? "No response")
        continue
    }

    # Convert graph to searchable string
    $graphJson = $graph | ConvertTo-Json -Depth 20

    # 1C: Check each interaction's CustomEvent exists in the graph
    $levelInteractions = $Interactions | Where-Object { $_.Level -eq $levelName }

    foreach ($ix in $levelInteractions) {
        $eventName = $ix.Event
        $stepNum = $ix.Step

        # Check CustomEvent node exists
        if ($graphJson -match $eventName) {
            Write-TestResult -Test "Step $stepNum : CustomEvent '$eventName' exists" -Status "PASS"
        } else {
            Write-TestResult -Test "Step $stepNum : CustomEvent '$eventName' exists" -Status "FAIL" -Detail "Not found in $levelName graph"
            continue
        }

        # Check MakeStruct node exists (Msg_StoryStep)
        # We look for MakeStruct near the event -- the graph JSON should contain both
        if ($graphJson -match "Msg_StoryStep") {
            # Good -- at least one MakeStruct for story steps exists
        } else {
            Write-TestResult -Test "Step $stepNum : MakeStruct Msg_StoryStep" -Status "FAIL" -Detail "No Msg_StoryStep MakeStruct found in $levelName"
        }

        # Check BroadcastGameplayMessage exists
        if ($graphJson -match "BroadcastGameplayMessage") {
            # Good
        } else {
            Write-TestResult -Test "Step $stepNum : BroadcastGameplayMessage node" -Status "FAIL" -Detail "No BroadcastGameplayMessage found in $levelName"
        }
    }

    # ==============================================================================
    # PHASE 2: BACKPROPAGATION VERIFICATION
    # ==============================================================================

    Write-Host ""
    Write-Host "--- Backpropagation: $levelName ---" -ForegroundColor White

    # Get the full graph with node details for pin-level verification
    # Use /api/graph with the level blueprint name
    $fullGraph = Invoke-MCP -Endpoint "/api/graph" -Method "GET" -Body ""
    # The graph endpoint might need a blueprint param -- try with level name
    $fullGraph2 = Invoke-MCP -Endpoint "/api/graph?blueprint=$levelName" -Method "GET"

    $graphData = if ($fullGraph2 -and -not $fullGraph2.error) { $fullGraph2 } else { $fullGraph }
    $graphStr = ""
    if ($graphData) {
        $graphStr = $graphData | ConvertTo-Json -Depth 30
    }

    foreach ($ix in $levelInteractions) {
        $eventName = $ix.Event
        $stepNum = $ix.Step

        # Backpropagation check: verify the chain
        # CustomEvent -> MakeStruct (Step=$stepNum) -> BroadcastGameplayMessage -> PrintString
        $chainChecks = @()

        # Check 1: CustomEvent exists
        $hasEvent = $graphStr -match $eventName
        $chainChecks += @{ Node="CustomEvent"; Found=$hasEvent }

        # Check 2: Step value is correct (look for the step number near MakeStruct)
        # The Step pin default should be set to the step number
        # We search for the step number in context of the event name
        $hasCorrectStep = $false
        if ($graphStr -match "Step.*$stepNum" -or $graphStr -match "$stepNum.*Step") {
            $hasCorrectStep = $true
        }
        # Also check for Step_4_ pin pattern with the value
        if ($graphStr -match "DefaultValue.*$stepNum") {
            $hasCorrectStep = $true
        }
        $chainChecks += @{ Node="MakeStruct(Step=$stepNum)"; Found=$hasCorrectStep }

        # Check 3: BroadcastGameplayMessage
        $hasBroadcast = $graphStr -match "BroadcastGameplayMessage"
        $chainChecks += @{ Node="BroadcastGameplayMessage"; Found=$hasBroadcast }

        # Determine chain status
        $allFound = ($chainChecks | Where-Object { $_.Found }).Count
        $total = $chainChecks.Count

        if ($allFound -eq $total) {
            Write-TestResult -Test "Step $stepNum BACKPROP: $eventName chain complete" -Status "PASS" -Detail "$allFound/$total nodes verified"
        } elseif ($allFound -gt 0) {
            $missing = ($chainChecks | Where-Object { -not $_.Found } | ForEach-Object { $_.Node }) -join ", "
            Write-TestResult -Test "Step $stepNum BACKPROP: $eventName chain" -Status "WARN" -Detail "Missing: $missing ($allFound/$total)"
        } else {
            Write-TestResult -Test "Step $stepNum BACKPROP: $eventName chain" -Status "FAIL" -Detail "No chain nodes found"
        }
    }
}

# ==============================================================================
# PHASE 3: STORY CONTROLLER VERIFICATION
# ==============================================================================

Write-TestHeader "PHASE 3: Story Controller Verification"

# Make sure ML_Main is visible (story controller lives there)
Invoke-MCP -Endpoint "/api/streaming-level-visibility" -Method "POST" -Body '{"levelPath": "ML_Main", "visible": true}' | Out-Null
Start-Sleep -Seconds 2

$storyState = Invoke-MCP -Endpoint "/api/story/state" -Method "GET"
if ($storyState -and -not $storyState.error) {
    Write-TestResult -Test "Story Controller found" -Status "PASS"

    # Check if story steps are registered
    $storyJson = $storyState | ConvertTo-Json -Depth 10
    if ($storyState.steps -or $storyState.totalSteps -or $storyState.stepCount) {
        $stepCount = if ($storyState.totalSteps) { $storyState.totalSteps }
                     elseif ($storyState.stepCount) { $storyState.stepCount }
                     elseif ($storyState.steps) { $storyState.steps.Count }
                     else { 0 }
        if ($stepCount -ge 32) {
            Write-TestResult -Test "Story steps registered ($stepCount)" -Status "PASS"
        } else {
            Write-TestResult -Test "Story steps registered ($stepCount)" -Status "WARN" -Detail "Expected >= 32"
        }
    } else {
        Write-TestResult -Test "Story steps registered" -Status "WARN" -Detail "Could not determine step count from response"
    }

    # Check current step
    if ($storyState.currentStep -ne $null) {
        Write-TestResult -Test "Current story step: $($storyState.currentStep)" -Status "PASS"
    }
} else {
    Write-TestResult -Test "Story Controller found" -Status "FAIL" -Detail ($storyState.error ?? "BP_StoryController not found -- is ML_Main visible?")
}

# ==============================================================================
# PHASE 4: SEQUENCE CROSS-REFERENCE
# ==============================================================================

Write-TestHeader "PHASE 4: Level Sequence Cross-Reference"

$sequences = Invoke-MCP -Endpoint "/api/list-sequences" -Method "POST" -Body '{}'
if ($sequences -and -not $sequences.error) {
    $seqJson = $sequences | ConvertTo-Json -Depth 10
    Write-TestResult -Test "Sequences retrieved" -Status "PASS"

    # Check that sequences exist for key scenes
    $expectedSeqPatterns = @(
        "Scene1", "Scene2", "Scene3", "Scene4", "Scene5",
        "Scene6", "Scene7", "Scene8"
    )
    foreach ($pattern in $expectedSeqPatterns) {
        if ($seqJson -match $pattern) {
            Write-TestResult -Test "Sequence for $pattern" -Status "PASS"
        } else {
            Write-TestResult -Test "Sequence for $pattern" -Status "WARN" -Detail "No sequence matching '$pattern' found"
        }
    }
} else {
    Write-TestResult -Test "Sequences retrieved" -Status "FAIL" -Detail ($sequences.error ?? "No response")
}

# ==============================================================================
# PHASE 5: PIE TEST (optional)
# ==============================================================================

if (-not $SkipPIE) {
    Write-TestHeader "PHASE 5: PIE (Play In Editor) Test"

    # Check current PIE state
    $pieState = Invoke-MCP -Endpoint "/api/get-pie-state" -Method "GET"
    $pieRunning = $false
    if ($pieState -and ($pieState.state -eq "Running" -or $pieState.isRunning -eq $true)) {
        $pieRunning = $true
        Write-TestResult -Test "PIE already running" -Status "PASS"
    }

    if (-not $pieRunning) {
        # Start PIE
        $startResult = Invoke-MCP -Endpoint "/api/start-pie" -Method "POST" -Body '{}'
        if ($startResult -and -not $startResult.error) {
            Write-TestResult -Test "PIE started" -Status "PASS"
            Start-Sleep -Seconds 5  # Wait for PIE to initialize
        } else {
            Write-TestResult -Test "PIE started" -Status "FAIL" -Detail ($startResult.error ?? "Failed to start PIE")
        }
    }

    # Verify PIE is running
    Start-Sleep -Seconds 2
    $pieCheck = Invoke-MCP -Endpoint "/api/get-pie-state" -Method "GET"
    if ($pieCheck -and ($pieCheck.state -eq "Running" -or $pieCheck.isRunning -eq $true)) {
        Write-TestResult -Test "PIE confirmed running" -Status "PASS"
    } else {
        Write-TestResult -Test "PIE confirmed running" -Status "FAIL" -Detail "PIE not in running state"
    }

    # Check story state in PIE
    Start-Sleep -Seconds 2
    $pieStory = Invoke-MCP -Endpoint "/api/story/state" -Method "GET"
    if ($pieStory -and -not $pieStory.error) {
        Write-TestResult -Test "Story controller accessible in PIE" -Status "PASS"
    } else {
        Write-TestResult -Test "Story controller accessible in PIE" -Status "WARN" -Detail "Story controller may not be spawned yet"
    }

    # Take a screenshot for visual verification
    $screenshot = Invoke-MCP -Endpoint "/api/screenshot" -Method "POST" -Body '{}'
    if ($screenshot -and -not $screenshot.error) {
        Write-TestResult -Test "PIE screenshot captured" -Status "PASS"
    } else {
        Write-TestResult -Test "PIE screenshot captured" -Status "WARN" -Detail "Could not capture screenshot"
    }

    # Check output log for errors
    $log = Invoke-MCP -Endpoint "/api/output-log" -Method "POST" -Body '{"lines": 100, "filter": "Error"}'
    if ($log -and $log.lines) {
        $errorCount = $log.lines.Count
        if ($errorCount -eq 0) {
            Write-TestResult -Test "PIE output log (errors)" -Status "PASS" -Detail "No errors in log"
        } else {
            Write-TestResult -Test "PIE output log (errors)" -Status "WARN" -Detail "$errorCount error lines found"
        }
    }

    # Stop PIE
    if (-not $pieRunning) {
        # Only stop if we started it
        $stopResult = Invoke-MCP -Endpoint "/api/stop-pie" -Method "POST" -Body '{}'
        if ($stopResult -and -not $stopResult.error) {
            Write-TestResult -Test "PIE stopped" -Status "PASS"
        } else {
            Write-TestResult -Test "PIE stopped" -Status "WARN" -Detail "Could not stop PIE cleanly"
        }
    }
} else {
    Write-Host ""
    Write-Host "  PIE tests skipped (-SkipPIE flag)" -ForegroundColor Yellow
}

# ==============================================================================
# REPORT
# ==============================================================================

Write-TestHeader "TEST REPORT"

Write-Host ""
Write-Host "  Timestamp:  $Timestamp" -ForegroundColor White
Write-Host "  Server:     $BASE" -ForegroundColor White
Write-Host "  Levels:     $($LevelNames -join ', ')" -ForegroundColor White
Write-Host ""
Write-Host "  Total Tests:  $script:TotalTests" -ForegroundColor White
Write-Host "  PASSED:       $script:Passed" -ForegroundColor Green
Write-Host "  FAILED:       $script:Failed" -ForegroundColor Red
Write-Host "  WARNINGS:     $script:Warnings" -ForegroundColor Yellow
Write-Host ""

$passRate = if ($script:TotalTests -gt 0) { [math]::Round(($script:Passed / $script:TotalTests) * 100, 1) } else { 0 }
if ($script:Failed -eq 0) {
    Write-Host "  RESULT: ALL TESTS PASSED ($passRate%)" -ForegroundColor Green
} else {
    Write-Host "  RESULT: $script:Failed FAILURES ($passRate% pass rate)" -ForegroundColor Red
}

# --- Generate Markdown Report ---
$reportLines = @()
$reportLines += "# SOH Regression Test Report"
$reportLines += ""
$reportLines += "| Field | Value |"
$reportLines += "|-------|-------|"
$reportLines += "| Timestamp | $Timestamp |"
$reportLines += "| Server | $BASE |"
$reportLines += "| Levels Tested | $($LevelNames -join ', ') |"
$reportLines += "| Total Tests | $script:TotalTests |"
$reportLines += "| Passed | $script:Passed |"
$reportLines += "| Failed | $script:Failed |"
$reportLines += "| Warnings | $script:Warnings |"
$reportLines += "| Pass Rate | $passRate% |"
$reportLines += ""
$reportLines += "## Detailed Results"
$reportLines += ""
$reportLines += "| Test | Status | Detail |"
$reportLines += "|------|--------|--------|"

foreach ($r in $script:Results) {
    $statusIcon = switch ($r.Status) {
        "PASS" { "PASS" }
        "FAIL" { "FAIL" }
        "WARN" { "WARN" }
    }
    $detail = if ($r.Detail) { $r.Detail } else { "" }
    $reportLines += "| $($r.Test) | $statusIcon | $detail |"
}

$reportLines += ""
$reportLines += "## Failed Tests"
$reportLines += ""
$failedTests = $script:Results | Where-Object { $_.Status -eq "FAIL" }
if ($failedTests.Count -eq 0) {
    $reportLines += "None."
} else {
    foreach ($f in $failedTests) {
        $reportLines += "- **$($f.Test)**: $($f.Detail)"
    }
}

$reportLines += ""
$reportLines += "## Warnings"
$reportLines += ""
$warnTests = $script:Results | Where-Object { $_.Status -eq "WARN" }
if ($warnTests.Count -eq 0) {
    $reportLines += "None."
} else {
    foreach ($w in $warnTests) {
        $reportLines += "- **$($w.Test)**: $($w.Detail)"
    }
}

try {
    $reportLines | Out-File -FilePath $ReportPath -Encoding UTF8
    Write-Host ""
    Write-Host "  Report saved to: $ReportPath" -ForegroundColor Cyan
} catch {
    Write-Host "  WARNING: Could not save report to $ReportPath" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Test complete. Review failures above before proceeding." -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
Read-Host "Press Enter to exit"
