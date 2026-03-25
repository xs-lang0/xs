const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const vscode = require("vscode");

let client;

function activate(context) {
  const bin = vscode.workspace.getConfiguration("xs").get("path", "xs");

  const serverOptions = {
    command: bin,
    args: ["lsp"],
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "xs" }],
  };

  client = new LanguageClient("xs", "XS Language Server", serverOptions, clientOptions);
  client.start();
}

function deactivate() {
  return client?.stop();
}

module.exports = { activate, deactivate };
