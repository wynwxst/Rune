// The Rune extension: the grammar and language configuration are declared in
// package.json; this file starts the language server and wires up commands.
//
// The server is `rune lsp` (or `rune-lsp` directly), found through the
// `rune.server.path` setting or on PATH. Everything else — diagnostics,
// completion, hover, navigation, quick fixes — comes from it.

"use strict";

const path = require("path");
const vscode = require("vscode");
const { LanguageClient, TransportKind, State } = require("vscode-languageclient/node");

/** @type {LanguageClient | undefined} */
let client;
/** @type {vscode.LogOutputChannel | undefined} */
let output;

/** How to start the server: `rune lsp`, or `rune-lsp` when that is named. */
function serverCommand() {
  const config = vscode.workspace.getConfiguration("rune");
  const configured = (config.get("server.path") || "").trim();
  const extra = config.get("server.extraArgs") || [];
  const command = configured || "rune";
  const base = path.basename(command).toLowerCase().replace(/\.exe$/, "");
  const args = base === "rune-lsp" ? [] : ["lsp"];
  return { command, args: args.concat(extra) };
}

/** The settings the server reads, as it reads them. */
function serverSettings() {
  const config = vscode.workspace.getConfiguration("rune");
  return {
    stdlib: config.get("stdlib") || "",
    checkOnSave: config.get("checkOnSave", true),
    check: {
      onChange: config.get("check.onChange", true),
      delay: config.get("check.delay", 400),
      memory: config.get("check.memory", "arc"),
    },
    inlayHints: {
      types: config.get("inlayHints.types", true),
      parameters: config.get("inlayHints.parameters", true),
    },
    lint: {
      enable: config.get("lint.enable", true),
      allow: config.get("lint.allow") || [],
      warn: config.get("lint.warn") || [],
      maxLineLength: config.get("lint.maxLineLength") || 0,
    },
  };
}

async function startClient(context) {
  const { command, args } = serverCommand();
  const run = { command, args, transport: TransportKind.stdio };
  const serverOptions = { run, debug: run };
  const clientOptions = {
    documentSelector: [
      { scheme: "file", language: "rune" },
      { scheme: "untitled", language: "rune" },
    ],
    initializationOptions: serverSettings(),
    // Files changed outside the editor — a `git checkout`, a build — are
    // re-read, so what they declare stays current. Settings are sent by
    // hand, below, in the shape the server reads.
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/{Rune.toml,*.rune}"),
    },
    outputChannel: output,
    traceOutputChannel: output,
  };

  client = new LanguageClient("rune", "Rune Language Server", serverOptions, clientOptions);
  try {
    await client.start();
  } catch (err) {
    client = undefined;
    const reason = err && err.message ? err.message : String(err);
    output.error(`could not start \`${[command, ...args].join(" ")}\`: ${reason}`);
    // ENOENT is the one failure PATH explains; anything else is the server's.
    const hint = /ENOENT/.test(reason)
      ? "It was not found: build the toolchain and put it on PATH, or set `rune.server.path`."
      : "See the output channel for details.";
    const pick = await vscode.window.showErrorMessage(
      `Rune: could not start the language server (\`${[command, ...args].join(" ")}\`): ${reason}. ${hint}`,
      "Open Settings",
      "Show Output"
    );
    if (pick === "Open Settings") {
      vscode.commands.executeCommand("workbench.action.openSettings", "rune.server.path");
    } else if (pick === "Show Output") {
      output.show();
    }
  }
}

async function stopClient() {
  if (!client) return;
  const c = client;
  client = undefined;
  if (c.state === State.Running) {
    await c.stop();
  }
}

/** Applies the server's "fix all" source action to the active editor. */
async function fixAll() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "rune") return;
  const whole = new vscode.Range(0, 0, editor.document.lineCount, 0);
  const actions = await vscode.commands.executeCommand(
    "vscode.executeCodeActionProvider",
    editor.document.uri,
    whole,
    "source.fixAll.rune"
  );
  const action = (actions || []).find((a) => a.kind && a.kind.value === "source.fixAll.rune");
  if (!action || !action.edit) {
    vscode.window.showInformationMessage("Rune: nothing to fix.");
    return;
  }
  await vscode.workspace.applyEdit(action.edit);
}

async function activate(context) {
  // A *log* channel: vscode-languageclient 10 reports through `.info()`,
  // `.error()` and friends, which a plain output channel does not have — and
  // a client whose logging throws fails in ways that look like anything but.
  output = vscode.window.createOutputChannel("Rune Language Server", { log: true });
  context.subscriptions.push(output);

  context.subscriptions.push(
    vscode.commands.registerCommand("rune.restartServer", async () => {
      await stopClient();
      await startClient(context);
    }),
    vscode.commands.registerCommand("rune.showOutput", () => output.show()),
    vscode.commands.registerCommand("rune.fixAll", fixAll),
    vscode.workspace.onDidChangeConfiguration(async (e) => {
      // Where the server is cannot change under a running one.
      if (e.affectsConfiguration("rune.server")) {
        await stopClient();
        await startClient(context);
      } else if (e.affectsConfiguration("rune") && client) {
        client.sendNotification("workspace/didChangeConfiguration", {
          settings: { rune: serverSettings() },
        });
      }
    })
  );

  await startClient(context);
}

async function deactivate() {
  await stopClient();
}

module.exports = { activate, deactivate };
