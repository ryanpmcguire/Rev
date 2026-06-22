# Build & tools

- Verify code compiles with the clever transpiler, no `--run`:
  `python clever/clever.py transpile ./MachineController/clever.json --no-embed -j20`
  (use the matching `clever.json` for whichever project you changed). The user runs/tests the actual app.
- A link-step "permission denied" on the `.exe` means the app is open and holding the file lock — it is not a code error. Retry once the user closes it.
- Modify files only with the Edit/Write tools. Never use bash/python/PowerShell scripts to edit, generate, or rewrite source files.
- Prefer the dedicated tools (Read, Edit, Grep, Glob) over shell equivalents (cat, sed, grep, find).
