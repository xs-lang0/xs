const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");

let client;

function findXsBinary() {
  const configured = vscode.workspace.getConfiguration("xs").get("path", "");
  if (configured) return configured;

  // common locations on windows
  const candidates = [
    "xs",
    path.join(process.env.USERPROFILE || "", "Desktop", "Claude", "xsypy", "xs.exe"),
    path.join(process.env.USERPROFILE || "", "Desktop", "Claude", "xsypy", "xs"),
    "C:\\xs\\xs.exe",
    "/usr/local/bin/xs",
  ];

  for (const p of candidates) {
    try {
      if (p !== "xs" && fs.existsSync(p)) return p;
    } catch {}
  }

  return "xs";
}

function activate(context) {
  const bin = findXsBinary();

  const serverOptions = {
    command: bin,
    args: ["lsp"],
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "xs" }],
  };

  client = new LanguageClient("xs", "XS Language Server", serverOptions, clientOptions);
  client.start().catch((err) => {
    const msg = `XS Language Server failed to start: ${err.message}. Set "xs.path" in settings to the full path of your xs binary.`;
    vscode.window.showWarningMessage(msg);
  });
}

function deactivate() {
  return client?.stop();
}

module.exports = { activate, deactivate };
