const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const { execFile } = require("child_process");

let client;
let outputChannel;
let statusItem;

function findXsBinary() {
  const configured = vscode.workspace.getConfiguration("xs").get("path", "");
  if (configured && configured !== "xs") {
    if (fs.existsSync(configured)) return configured;
    if (process.platform === "win32" && !configured.endsWith(".exe")) {
      const withExe = configured + ".exe";
      if (fs.existsSync(withExe)) return withExe;
    }
  }

  const extDir = path.dirname(__dirname);
  const candidates = [
    path.resolve(extDir, "..", "..", "xs.exe"),
    path.resolve(extDir, "..", "..", "xs"),
    path.join(process.env.USERPROFILE || "", "Desktop", "Claude", "xsypy", "xs.exe"),
    "C:\\xs\\xs.exe",
    "/usr/local/bin/xs",
  ];

  for (const p of candidates) {
    try {
      if (fs.existsSync(p)) return p;
    } catch {}
  }

  return process.platform === "win32" ? "xs.exe" : "xs";
}

// quote path for the terminal shell — handles spaces and backslashes
function shellQuote(p) {
  // for PowerShell/cmd on windows, use & operator with quoted path
  if (process.platform === "win32") {
    return `& '${p}'`;
  }
  return `'${p}'`;
}

class XsDebugAdapterFactory {
  createDebugAdapterDescriptor(_session) {
    const bin = findXsBinary();
    return new vscode.DebugAdapterExecutable(bin, ["dap"]);
  }
}

function stripAnsi(str) {
  return str.replace(/\x1b\[[0-9;]*m/g, "");
}

function activate(context) {
  const bin = findXsBinary();
  outputChannel = vscode.window.createOutputChannel("XS");

  // status bar
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  statusItem.text = "$(zap) XS";
  statusItem.tooltip = "XS Language Server";
  statusItem.command = "xs.showOutput";
  statusItem.show();
  context.subscriptions.push(statusItem);

  // commands
  context.subscriptions.push(
    vscode.commands.registerCommand("xs.showOutput", () => outputChannel.show()),

    vscode.commands.registerCommand("xs.runFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        // use execFile to run in output channel, not terminal (avoids WSL path issues)
        outputChannel.show();
        outputChannel.appendLine(`--- Running ${path.basename(file)} ---`);
        execFile(bin, [file], { cwd: path.dirname(file), timeout: 30000 }, (err, stdout, stderr) => {
          if (stdout) outputChannel.appendLine(stripAnsi(stdout));
          if (stderr) outputChannel.appendLine(stripAnsi(stderr));
          if (err && err.killed) outputChannel.appendLine("(timed out after 30s)");
          outputChannel.appendLine("--- Done ---");
        });
      });
    }),

    vscode.commands.registerCommand("xs.runFileVM", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        outputChannel.show();
        outputChannel.appendLine(`--- Running ${path.basename(file)} (VM) ---`);
        execFile(bin, ["--vm", file], { cwd: path.dirname(file), timeout: 30000 }, (err, stdout, stderr) => {
          if (stdout) outputChannel.appendLine(stripAnsi(stdout));
          if (stderr) outputChannel.appendLine(stripAnsi(stderr));
          outputChannel.appendLine("--- Done ---");
        });
      });
    }),

    vscode.commands.registerCommand("xs.buildFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        const out = file.replace(/\.xs$/, ".xsc");
        execFile(bin, ["build", file, "-o", out], { cwd: path.dirname(file) }, (err, stdout, stderr) => {
          const msg = stripAnsi(stderr || stdout || "");
          outputChannel.appendLine(msg || "build complete");
          if (!err) vscode.window.showInformationMessage(`Compiled: ${path.basename(out)}`);
          else vscode.window.showErrorMessage(`Build failed`);
        });
      });
    }),

    vscode.commands.registerCommand("xs.checkFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        execFile(bin, ["--no-color", "--check", file], { cwd: path.dirname(file) }, (err, stdout, stderr) => {
          const output = stripAnsi(stderr || stdout || "");
          outputChannel.clear();
          if (output) outputChannel.appendLine(output);
          if (!err) vscode.window.showInformationMessage("No type errors found");
          else {
            vscode.window.showWarningMessage("Type errors found — see XS output");
            outputChannel.show();
          }
        });
      });
    }),

    vscode.commands.registerCommand("xs.formatFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      // trigger LSP formatting directly
      vscode.commands.executeCommand("editor.action.formatDocument");
    }),

    vscode.commands.registerCommand("xs.restartServer", async () => {
      if (client) {
        statusItem.text = "$(sync~spin) XS";
        try {
          await client.stop();
          await client.start();
          statusItem.text = "$(zap) XS";
          vscode.window.showInformationMessage("XS Language Server restarted");
        } catch (err) {
          statusItem.text = "$(warning) XS";
          vscode.window.showErrorMessage(`Failed to restart: ${err.message}`);
        }
      }
    }),

    vscode.commands.registerCommand("xs.openRepl", () => {
      outputChannel.show();
      outputChannel.appendLine("XS REPL not available in output channel — use a terminal:");
      outputChannel.appendLine(`  ${bin}`);
      // open a proper terminal
      const terminal = vscode.window.createTerminal({ name: "XS REPL", shellPath: bin });
      terminal.show();
    })
  );

  // LSP
  const serverOptions = {
    command: bin,
    args: ["lsp"],
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "xs" }],
    outputChannel,
  };

  client = new LanguageClient("xs", "XS Language Server", serverOptions, clientOptions);
  client.start().then(() => {
    statusItem.text = "$(zap) XS";
    statusItem.tooltip = "XS Language Server — running";
    outputChannel.appendLine("XS Language Server started");
  }).catch((err) => {
    statusItem.text = "$(warning) XS";
    statusItem.tooltip = "XS Language Server — failed";
    const msg = `XS Language Server failed to start: ${err.message}. Set "xs.path" in settings.`;
    vscode.window.showWarningMessage(msg);
    outputChannel.appendLine(msg);
  });

  // DAP
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("xs", new XsDebugAdapterFactory())
  );

  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider("xs", {
      resolveDebugConfiguration(_folder, config) {
        if (!config.type && !config.request && !config.name) {
          const editor = vscode.window.activeTextEditor;
          if (editor && editor.document.languageId === "xs") {
            config.type = "xs";
            config.name = "Debug XS";
            config.request = "launch";
            config.program = "${file}";
            config.stopOnEntry = true;
          }
        }
        if (!config.program) {
          return vscode.window.showInformationMessage("No XS file to debug").then(() => undefined);
        }
        // default to stop on entry so the debugger doesn't exit immediately
        if (config.stopOnEntry === undefined) {
          config.stopOnEntry = true;
        }
        return config;
      },
    })
  );

  // code lens
  context.subscriptions.push(
    vscode.languages.registerCodeLensProvider({ language: "xs" }, {
      provideCodeLenses(document) {
        if (!vscode.workspace.getConfiguration("xs").get("codeLens.enabled", true)) return [];
        const lenses = [];
        lenses.push(new vscode.CodeLens(new vscode.Range(0, 0, 0, 0), {
          title: "$(play) Run",
          command: "xs.runFile",
        }));
        lenses.push(new vscode.CodeLens(new vscode.Range(0, 0, 0, 0), {
          title: "$(bug) Debug",
          command: "workbench.action.debug.start",
        }));
        if (document.fileName.includes("test_")) {
          lenses.push(new vscode.CodeLens(new vscode.Range(0, 0, 0, 0), {
            title: "$(beaker) Run Tests",
            command: "xs.runFile",
          }));
        }
        return lenses;
      },
    })
  );
}

function deactivate() {
  if (outputChannel) outputChannel.dispose();
  return client?.stop();
}

module.exports = { activate, deactivate };
