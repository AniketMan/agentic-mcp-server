# User Context Directory

**This directory is for YOU (the user).**

Place any project-specific files here that you want the local Worker model to know about.

**The system will NEVER modify files in this directory.** It only reads them.

## What to put here:

1. **`script.md`** - The narrative script, dialogue, or level flow.
2. **`roadmap.md`** - Your high-level goals and step-by-step feature checklist.
3. **`ContentBrowser_Hierarchy.txt`** - A text dump of your Unreal Engine asset paths. *(If an asset isn't in this file, the Worker will assume it doesn't exist unless it queries the engine).*
4. **`notes.md`** - Any specific rules, naming conventions, or workarounds you want the Worker to follow.

## Chat Logs

The TUI automatically saves chat session logs to `chat_logs/` in this directory. These provide persistent memory across sessions -- the Worker reads them on startup to maintain context from previous conversations.

## How it works:

When you start a session, the Worker reads everything in this folder. It uses your files as the **absolute ground truth** for inference. If your files contradict the model's training data, your files win.

If you leave this folder empty, the Worker operates with only the tool registry and engine docs as context.
