#!/usr/bin/env node

import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';
import {
  log,
  logSection,
  assertTestProject,
  engineRootFromEnginePath,
  getProjectPaths,
  protectedEngineRoots,
  isSameOrUnder,
  resolveTestEngine,
} from './build-utils.js';

/**
 * UE_EDITOR_PATH stays supported so an existing setup keeps working, but it
 * goes through the same protected-root deny list as a discovered engine.
 * Otherwise a stale override would be the one way around the guard.
 */
function editorFromOverride(overridePath) {
  const editorExecutable = path.resolve(overridePath);
  const engineRoot = engineRootFromEnginePath(editorExecutable) ?? path.dirname(editorExecutable);
  const roots = protectedEngineRoots();
  const match = roots.find((root) => isSameOrUnder(engineRoot, root));

  if (match) {
    throw new Error(
      `UE_EDITOR_PATH resolves under protected engine root '${match}'. Refusing to launch the test project from it.`
    );
  }

  return { editorExecutable, engineRoot, engineRootSource: 'UE_EDITOR_PATH' };
}

async function main() {
  logSection('UE-MCP Run');

  const { projectFile } = getProjectPaths();
  let engine;
  try {
    assertTestProject(projectFile);
    engine = process.env.UE_EDITOR_PATH
      ? editorFromOverride(process.env.UE_EDITOR_PATH)
      : resolveTestEngine();
  } catch (error) {
    log(`ERROR: ${error.message}`, 'red');
    process.exit(1);
  }

  const editorExe = engine.editorExecutable;

  if (!fs.existsSync(editorExe)) {
    log(`ERROR: Unreal Editor executable not found: ${editorExe}`, 'red');
    log(`Resolved from ${engine.engineRootSource}. Set UE_MCP_TEST_ENGINE_ROOT or UE_EDITOR_PATH to correct it.`);
    process.exit(1);
  }

  log(`Project File: ${projectFile}`);
  log(`Engine Root: ${engine.engineRoot} (from ${engine.engineRootSource})`);
  log(`Editor: ${editorExe}`);
  log('');

  log('Launching Unreal Editor...', 'green');

  // Launch editor with project file.
  //
  // stdio is ignored, not inherited: a detached editor that inherits this
  // terminal holds its stdout open for its whole session, so `npm run up`
  // never returns when piped, and the editor's own logging would scribble
  // over the progress bar below. The editor writes Saved/Logs either way.
  const launchedAtMs = Date.now();
  const proc = spawn(editorExe, [projectFile], {
    stdio: 'ignore',
    detached: true,
  });

  proc.unref(); // Allow parent process to exit

  // Same wait the start_editor tool performs: hold here with a progress bar
  // until the editor is actually usable, rather than printing "launched!" over
  // a splash screen that has forty seconds of module loading left.
  const { waitForEditorReadyExternal } = await import('../dist/editor-control.js');
  // The launch timestamp makes the wait ignore a port lockfile an earlier
  // session left behind, so readiness is judged on this editor's bridge.
  const result = await waitForEditorReadyExternal(projectFile, path.dirname(projectFile), 300, launchedAtMs);
  if (result.ready) {
    log(`Editor ready in ${result.elapsedSeconds.toFixed(1)}s`, 'green');
  } else {
    log(`Editor did not become ready: ${result.reason}`, 'yellow');
  }
}

main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
