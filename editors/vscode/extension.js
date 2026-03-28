const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const { execFile, spawn } = require("child_process");

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

// quote path for the terminal shell: handles spaces and backslashes
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
        outputChannel.clear();
        outputChannel.show(true);
        outputChannel.appendLine(`> ${path.basename(bin)} "${file}"`);
        outputChannel.appendLine("");
        const proc = spawn(bin, [file], { cwd: path.dirname(file), shell: true });
        proc.stdout.on("data", (data) => outputChannel.append(stripAnsi(data.toString())));
        proc.stderr.on("data", (data) => outputChannel.append(stripAnsi(data.toString())));
        proc.on("error", (err) => outputChannel.appendLine(`Error: ${err.message}`));
        proc.on("close", (code) => { if (code !== 0) outputChannel.appendLine(`\n[exit code: ${code}]`); });
      });
    }),

    vscode.commands.registerCommand("xs.runFileVM", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        outputChannel.clear();
        outputChannel.show(true);
        outputChannel.appendLine(`> ${bin} --vm "${file}"`);
        outputChannel.appendLine("");
        const proc = spawn(bin, ["--vm", file], { cwd: path.dirname(file), shell: true });
        proc.stdout.on("data", (data) => outputChannel.append(stripAnsi(data.toString())));
        proc.stderr.on("data", (data) => outputChannel.append(stripAnsi(data.toString())));
        proc.on("error", (err) => outputChannel.appendLine(`Error: ${err.message}`));
        proc.on("close", (code) => outputChannel.appendLine(`\n[exit code: ${code}]`));
      });
    }),

    vscode.commands.registerCommand("xs.buildFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xs") return;
      editor.document.save().then(() => {
        const file = editor.document.fileName;
        const out = file.replace(/\.xs$/, ".xsc");
        execFile(bin, ["build", file, "-o", out], { cwd: path.dirname(file), shell: true }, (err, stdout, stderr) => {
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
        execFile(bin, ["--no-color", "--check", file], { cwd: path.dirname(file), shell: true }, (err, stdout, stderr) => {
          outputChannel.clear();
          // diagnostics go to stderr, combine both
          const combined = [stdout, stderr].filter(Boolean).join("\n").trim();
          const clean = stripAnsi(combined);
          if (clean) outputChannel.appendLine(clean);
          if (!err) vscode.window.showInformationMessage("No type errors found");
          else {
            vscode.window.showWarningMessage("Type errors found: see XS output");
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
      statusItem.text = "$(sync~spin) XS";
      try {
        if (client) await client.stop();
      } catch {}
      try {
        const newBin = findXsBinary();
        client = new LanguageClient("xs", "XS Language Server", {
          command: newBin,
          args: lspArgs,
          transport: TransportKind.stdio,
        }, {
          documentSelector: [{ scheme: "file", language: "xs" }],
          outputChannel,
        });
        await client.start();
        statusItem.text = "$(zap) XS";
        vscode.window.showInformationMessage("XS Language Server restarted");
      } catch (err) {
        statusItem.text = "$(warning) XS";
        vscode.window.showErrorMessage(`Failed to restart: ${err.message}`);
      }
    }),

    vscode.commands.registerCommand("xs.openRepl", () => {
      const terminal = vscode.window.createTerminal({ name: "XS REPL", shellPath: bin, shellArgs: [] });
      terminal.show();
    })
  );

  // LSP: find bundled lsp.xs
  const extDir = path.dirname(__dirname);
  const lspCandidates = [
    path.resolve(extDir, "lsp.xs"),                    // bundled in extension
    path.resolve(extDir, "..", "..", "src", "lsp", "lsp.xs"),  // dev: repo root
  ];
  let lspSource = null;
  for (const p of lspCandidates) {
    try { if (fs.existsSync(p)) { lspSource = p; break; } } catch {}
  }

  const lspArgs = lspSource ? ["lsp", "-s", lspSource] : ["lsp"];
  const serverOptions = {
    command: bin,
    args: lspArgs,
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "xs" }],
    outputChannel,
  };

  client = new LanguageClient("xs", "XS Language Server", serverOptions, clientOptions);
  client.start().then(() => {
    statusItem.text = "$(zap) XS";
    statusItem.tooltip = "XS Language Server: running";
    outputChannel.appendLine("XS Language Server started");
  }).catch((err) => {
    statusItem.text = "$(warning) XS";
    statusItem.tooltip = "XS Language Server: failed";
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
