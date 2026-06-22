// Clever — minimal VSCode integration.
//
// * A dedicated "Clever" channel in the Output panel (like CMake/Build, NPM).
// * F5 builds into that channel BEFORE the debugger launches (program stdio
//   still goes to the Debug Console, owned by cppvsdbg).
// * A notion of the "current" clever project: walk up from the active file to
//   the nearest clever.json. "Clever: Set Current" pins it; otherwise it is
//   auto-detected from whatever file you're in. Build + debug follow it, and
//   the debug target's exe/cwd are derived from the manifest, so ONE generic
//   launch config works for every project.
//
// Zero-build: plain CommonJS, only Node's child_process/fs/path + the built-in
// `vscode` module. No npm install, no compile step.

const vscode = require("vscode");
const cp = require("child_process");
const fs = require("fs");
const path = require("path");

let channel;        // the dedicated "Clever" Output channel
let statusItem;     // status-bar indicator of the current project
let targetItem;     // status-bar indicator of the current build target/config
let diagnostics;    // clang errors/warnings surfaced in the Problems panel
let ctx;            // extension context (for workspaceState persistence)

const PINNED_KEY = "clever.currentManifest";
const CONFIG_KEY = "clever.currentConfig";   // selected build config (or "" = base)

// clang diagnostic line: "<path>:<line>:<col>: error|warning: <message>".
// The leading drive colon (C:\...) is skipped because it isn't followed by
// digits, so the non-greedy path stops at the real line:col:.
const DIAG_RE = /^(.*?):(\d+):(\d+):\s+(error|warning):\s+(.*)$/;

// Parse a finished build's output and republish the Problems panel. clever's
// #line directives mean clang already reports against the original .ixx paths,
// so these map straight to the files you edit.
function updateDiagnostics(text) {
  if (!diagnostics) return;
  diagnostics.clear();
  const byFile = new Map();      // fsPath -> { uri, items, seen }
  for (const line of text.split(/\r?\n/)) {
    const m = DIAG_RE.exec(line);
    if (!m) continue;
    const [, file, ln, col, sev, msg] = m;
    let uri;
    try { uri = vscode.Uri.file(file); } catch (e) { continue; }
    let bucket = byFile.get(uri.fsPath);
    if (!bucket) { bucket = { uri, items: [], seen: new Set() }; byFile.set(uri.fsPath, bucket); }
    const dedupe = ln + ":" + col + ":" + sev + ":" + msg;
    if (bucket.seen.has(dedupe)) continue;       // clang repeats per including TU
    bucket.seen.add(dedupe);
    const pos = new vscode.Position(Math.max(0, +ln - 1), Math.max(0, +col - 1));
    const range = new vscode.Range(pos, pos.with(undefined, Number.MAX_SAFE_INTEGER));
    const diag = new vscode.Diagnostic(
      range, msg,
      sev === "error" ? vscode.DiagnosticSeverity.Error
                      : vscode.DiagnosticSeverity.Warning);
    diag.source = "clever";
    bucket.items.push(diag);
  }
  for (const b of byFile.values()) diagnostics.set(b.uri, b.items);
}

function getChannel() {
  if (!channel) channel = vscode.window.createOutputChannel("Clever");
  return channel;
}

function workspaceRoot(folder) {
  if (folder && folder.uri) return folder.uri.fsPath;
  const ws = vscode.workspace.workspaceFolders;
  return ws && ws.length ? ws[0].uri.fsPath : undefined;
}

function samePath(a, b) {
  return a && b && path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
}

// Walk UP from a starting file's directory to the nearest clever.json, without
// climbing above the workspace root. Returns the manifest path or undefined.
function findManifestUp(startFile, root) {
  let dir = path.dirname(startFile);
  for (;;) {
    const candidate = path.join(dir, "clever.json");
    if (fs.existsSync(candidate)) return candidate;
    if (root && samePath(dir, root)) return undefined; // checked root, stop
    const parent = path.dirname(dir);
    if (parent === dir) return undefined;              // filesystem root
    dir = parent;
  }
}

// The manifest to act on: the pinned one if set, else auto-detected by walking
// up from the active editor's file.
function getActiveManifest() {
  const pinned = ctx.workspaceState.get(PINNED_KEY);
  if (pinned && fs.existsSync(pinned)) return pinned;
  const ed = vscode.window.activeTextEditor;
  if (ed && ed.document.uri.scheme === "file") {
    return findManifestUp(ed.document.uri.fsPath, workspaceRoot());
  }
  return undefined;
}

// Read a manifest and locate its executable target -> {program, cwd}.
// exe lives at <manifestDir>/.clever[/<config>]/out/<target.output>.
function deriveExe(manifest, config) {
  try {
    const m = JSON.parse(fs.readFileSync(manifest, "utf8"));
    const exe = (m.targets || []).find((t) => t.kind === "exe");
    if (!exe || !exe.output) return undefined;
    const sub = config ? path.join(".clever", config) : ".clever";
    const program = path.join(path.dirname(manifest), sub, "out", exe.output);
    return { program, cwd: path.dirname(program) };
  } catch (e) {
    return undefined;
  }
}

function projectName(manifest) {
  return manifest ? path.basename(path.dirname(manifest)) : undefined;
}

// Names of the configs a manifest declares (the build "targets" you pick from).
function manifestConfigs(manifest) {
  try {
    return Object.keys(JSON.parse(fs.readFileSync(manifest, "utf8")).configs || {});
  } catch (e) {
    return [];
  }
}

// The selected config, but only if the current project actually declares it
// (so a config picked for one project doesn't break F5 in another). "" = base.
function activeConfig(manifest) {
  const sel = ctx.workspaceState.get(CONFIG_KEY) || "";
  if (sel && manifest && !manifestConfigs(manifest).includes(sel)) return "";
  return sel;
}

function updateStatus() {
  if (!statusItem) return;
  const pinned = ctx.workspaceState.get(PINNED_KEY);
  const active = getActiveManifest();
  const name = projectName(active);
  if (!name) {
    statusItem.text = "$(tools) Clever: none";
    statusItem.tooltip = "No clever.json found — click to set the current project";
  } else if (pinned) {
    statusItem.text = "$(tools) Clever: " + name;
    statusItem.tooltip = "Pinned project: " + pinned + "\nClick to change";
  } else {
    statusItem.text = "$(tools) Clever: " + name + " (auto)";
    statusItem.tooltip = "Auto-detected from the active file: " + active +
      "\nClick to pin a project";
  }
  statusItem.show();

  if (targetItem) {
    const cfg = activeConfig(active);
    targetItem.text = "$(layers) " + (cfg || "base");
    targetItem.tooltip = "Clever build target for F5 (click to change)";
    if (name) targetItem.show(); else targetItem.hide();
  }
}

// Run `clever transpile <manifest>` from the workspace root, streaming all
// output into the Clever channel. Resolves true on exit code 0.
function runBuild(cwd, manifest, jobs, config) {
  return new Promise((resolve) => {
    const ch = getChannel();
    ch.clear();
    ch.show(true); // reveal the channel, but don't steal editor focus
    if (diagnostics) diagnostics.clear();
    const args = [
      "clever/clever.py", "transpile", manifest, "--no-embed",
      "-j" + (jobs || 8),
    ];
    if (config) args.push("--config", config);
    ch.appendLine("> python " + args.join(" "));
    ch.appendLine("");
    let buf = "";
    const feed = (d) => { const s = d.toString(); buf += s; ch.append(s); };
    let proc;
    try {
      proc = cp.spawn("python", args, { cwd });
    } catch (e) {
      ch.appendLine("[clever] failed to launch python: " + e.message);
      return resolve(false);
    }
    proc.stdout.on("data", feed);
    proc.stderr.on("data", feed);
    proc.on("error", (e) => { ch.appendLine("[clever] " + e.message); resolve(false); });
    proc.on("close", (code) => {
      ch.appendLine("");
      ch.appendLine("[clever] exit " + code);
      updateDiagnostics(buf);  // republish the Problems panel
      resolve(code === 0);
    });
  });
}

function activate(context) {
  ctx = context;

  diagnostics = vscode.languages.createDiagnosticCollection("clever");
  context.subscriptions.push(diagnostics);

  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  statusItem.command = "clever.setCurrent";
  context.subscriptions.push(statusItem);

  targetItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
  targetItem.command = "clever.changeTarget";
  context.subscriptions.push(targetItem);
  updateStatus();
  // Keep the (auto) indicator fresh as the user moves between files.
  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(() => updateStatus())
  );

  // "Clever: Set Current" — pin the project that owns the active file.
  context.subscriptions.push(
    vscode.commands.registerCommand("clever.setCurrent", async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed || ed.document.uri.scheme !== "file") {
        vscode.window.showErrorMessage("Clever: open a file inside a project first.");
        return;
      }
      const manifest = findManifestUp(ed.document.uri.fsPath, workspaceRoot());
      if (!manifest) {
        vscode.window.showErrorMessage(
          "Clever: no clever.json found above " + ed.document.uri.fsPath);
        return;
      }
      await ctx.workspaceState.update(PINNED_KEY, manifest);
      updateStatus();
      vscode.window.showInformationMessage(
        "Clever: current project is now " + projectName(manifest) + " (" + manifest + ")");
    })
  );

  // "Clever: Change Target" — pick which build config (debug/release/apple/…)
  // F5 builds and debugs. Choices come from the current manifest's `configs`.
  context.subscriptions.push(
    vscode.commands.registerCommand("clever.changeTarget", async () => {
      const manifest = getActiveManifest();
      if (!manifest) {
        vscode.window.showErrorMessage(
          "Clever: no current project — open a file in one or run 'Clever: Set Current'.");
        return;
      }
      const items = [{ label: "base", description: "no config — .clever/", val: "" }];
      for (const n of manifestConfigs(manifest)) {
        items.push({ label: n, description: ".clever/" + n, val: n });
      }
      const pick = await vscode.window.showQuickPick(items, {
        placeHolder: "Clever build target for F5 (" + projectName(manifest) + ")",
      });
      if (!pick) return;
      await ctx.workspaceState.update(CONFIG_KEY, pick.val);
      updateStatus();
      vscode.window.showInformationMessage("Clever target: " + (pick.val || "base"));
    })
  );

  // "Clever: Build" — build the current project (no prompt when one is active).
  context.subscriptions.push(
    vscode.commands.registerCommand("clever.build", async () => {
      const manifest = getActiveManifest();
      if (!manifest) {
        vscode.window.showErrorMessage(
          "Clever: no current project — open a file in one or run 'Clever: Set Current'.");
        return;
      }
      await runBuild(workspaceRoot(), manifest, undefined, activeConfig(manifest) || undefined);
    })
  );

  // F5 hook. A cppvsdbg config opts in by setting `cleverManifest`:
  //   * a path        -> build that exact project (and use the config's program)
  //   * "auto"/absent  -> build the CURRENT project and derive program/cwd from it
  const provider = {
    async resolveDebugConfiguration(folder, config) {
      if (!config || !("cleverManifest" in config)) return config;
      const explicit = config.cleverManifest && config.cleverManifest !== "auto"
        ? config.cleverManifest : undefined;
      const manifest = explicit || getActiveManifest();
      if (!manifest) {
        vscode.window.showErrorMessage(
          "Clever: no current project to debug — run 'Clever: Set Current'.");
        return undefined;
      }
      // Target precedence: the command-selected config (workspaceState) wins;
      // on first use it's seeded from the launch config's cleverConfig default.
      let cfg = ctx.workspaceState.get(CONFIG_KEY);
      if (cfg === undefined) {
        cfg = config.cleverConfig || "";
        await ctx.workspaceState.update(CONFIG_KEY, cfg);
        updateStatus();
      }
      if (cfg && !manifestConfigs(manifest).includes(cfg)) cfg = "";  // not in this project
      const ok = await runBuild(workspaceRoot(folder), manifest, config.cleverJobs,
                                cfg || undefined);
      if (!ok) {
        vscode.window.showErrorMessage(
          "Clever build failed — see the Clever output channel.");
        return undefined; // cancel the debug session
      }
      // Fill in program/cwd from the manifest when the config didn't pin them.
      if (!config.program) {
        const exe = deriveExe(manifest, cfg || undefined);
        if (!exe) {
          vscode.window.showErrorMessage(
            "Clever: no exe target found in " + manifest);
          return undefined;
        }
        config.program = exe.program;
        if (!config.cwd) config.cwd = exe.cwd;
      }
      return config;
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider("cppvsdbg", provider)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
