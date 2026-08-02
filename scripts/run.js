#!/usr/bin/env node

import { spawn } from 'child_process';
import fs from 'fs';
import { log, logSection, getProjectPaths, resolveTestEngine } from './build-utils.js';

async function main() {
  logSection('UE-MCP Run');

  const { projectFile } = getProjectPaths();
  let testEngine;
  try {
    testEngine = resolveTestEngine();
  } catch (error) {
    log(`ERROR: ${error.message}`, 'red');
    process.exit(1);
  }

  const editorExe = testEngine.editorExecutable;

  if (!fs.existsSync(editorExe)) {
    log(`ERROR: Unreal Editor executable not found in UE_MCP_TEST_ENGINE_ROOT: ${editorExe}`, 'red');
    process.exit(1);
  }

  log(`Project File: ${projectFile}`);
  log(`Test Engine Root: ${testEngine.engineRoot}`);
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
