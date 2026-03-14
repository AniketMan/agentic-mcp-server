# User Context Directory

**This directory is for YOU (the user).**

Place any project-specific files here that you want the Planner (Claude) and the local workers to know about. 

**Claude will NEVER modify files in this directory.** It only reads them.

## What to put here:

1. **`script.md`** - The narrative script, dialogue, or level flow.
2. **`roadmap.md`** - Your high-level goals and step-by-step feature checklist.
3. **`ContentBrowser_Hierarchy.txt`** - A text dump of your Unreal Engine asset paths. *(Crucial: If an asset isn't in this file, the Planner will assume it doesn't exist).*
4. **`notes.md`** - Any specific rules, naming conventions, or workarounds you want the agents to follow.

## How it works:
When you start a session, Claude reads everything in this folder. It uses your files as the **absolute ground truth** to generate the execution plan. If your files contradict Claude's training data, your files win.

If you leave this folder empty, Claude will ask you for instructions before proceeding.
