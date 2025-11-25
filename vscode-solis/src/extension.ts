import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
  const config = workspace.getConfiguration('solis');
  let serverPath = config.get<string>('lsp.serverPath');

  // If no custom path configured, try to find solis-lsp in workspace root
  if (!serverPath || serverPath === 'solis-lsp') {
    const workspaceFolders = workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
      const workspaceRoot = workspaceFolders[0].uri.fsPath;

      // Use wrapper script for debugging
      const wrapperPath = path.join(workspaceRoot, 'solis-lsp-wrapper.sh');
      const localServerPath = path.join(workspaceRoot, 'solis-lsp');

      // Check if wrapper exists (for debugging)
      const fs = require('fs');
      if (fs.existsSync(wrapperPath)) {
        serverPath = wrapperPath;
        console.log(`[Solis] Using wrapper script: ${serverPath}`);
      } else if (fs.existsSync(localServerPath)) {
        serverPath = localServerPath;
        console.log(`[Solis] Using local LSP server: ${serverPath}`);
      } else {
        serverPath = 'solis-lsp'; // Fall back to PATH
        console.log('[Solis] Using solis-lsp from PATH');
      }
    } else {
      serverPath = 'solis-lsp'; // Fall back to PATH
    }
  }

  console.log(`[Solis] Starting LSP server: ${serverPath}`);

  const serverOptions: ServerOptions = {
    command: serverPath,
    args: [],
    transport: TransportKind.stdio
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'solis' }],
    synchronize: {
      fileEvents: workspace.createFileSystemWatcher('**/*.solis')
    }
  };

  client = new LanguageClient(
    'solisLanguageServer',
    'Solis Language Server',
    serverOptions,
    clientOptions
  );

  // Add error handlers
  client.onDidChangeState((event) => {
    console.log(`[Solis] State changed: ${event.oldState} -> ${event.newState}`);
  });

  console.log('[Solis] Starting LSP client...');

  client.start().then(() => {
    console.log('[Solis] LSP client started successfully');
  }).catch((error) => {
    console.error('[Solis] Failed to start LSP client:', error);
    const msg = error.message || String(error);
    if (msg.includes('ENOENT') || msg.includes('not found')) {
      console.error(`[Solis] Server executable not found: ${serverPath}`);
      console.error('[Solis] Make sure solis-lsp is built with: make solis-lsp');
    }
  });
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  console.log('[Solis] Deactivating extension, stopping LSP client...');
  return client.stop();
}
