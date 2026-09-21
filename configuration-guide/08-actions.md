# 8. Actions

![Actions screen](images/08-actions.png)

Actions run external commands when selected VideoProcessor events occur. They are retained as drafts when disabled and have no implicit priority.

- **+ Add action** — Creates a named action entry.
- **Remove** — Deletes the selected action after confirmation.
- **Name** — Unique human-readable action name. Spaces are allowed.
- **Enable this action** — Controls whether the action can run. Disabled actions remain available for later editing.
- **Renderer target** — Runs for VP Renderer, a discovered renderer, or **All renderers**.
- **Group** — Coalescing group. Actions in the same group replace one another while waiting, so only the newest matching action runs. Leave it blank to keep this action independent.
- **Delay** — Wait time in seconds before launching the command. Use zero for an immediate action.
- **Run on these events** — One or more event triggers. A new action starts with no events selected.
- **Only run when (optional)** — Source/event condition such as `${eotf} == "PQ"`. Screen Config conditions use the visible profile name, for example `${screen_config} == "Scope"`; see [Rules](rules.md) for the full grammar and variable list.
- **Command line** — The executable, batch file, or command file to run, followed by optional arguments and supported `${variable}` placeholders.

Treat action commands as code execution. Use absolute paths, quote paths with spaces, and test a command manually before enabling it.

---

[← Configuration guide index](README.md)
