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

  // Launch editor with project file
  const proc = spawn(editorExe, [projectFile], {
    stdio: 'inherit',
    detached: true,
  });

  proc.unref(); // Allow parent process to exit
  log('Editor launched!', 'green');
}

main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
