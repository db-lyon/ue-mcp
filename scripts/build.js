#!/usr/bin/env node

import { spawn, exec } from 'child_process';
import { log, logSection, assertTestProject, createTestBuildPlan } from './build-utils.js';

function runCommand(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const isWindows = process.platform === 'win32';
    
    if (isWindows && command.endsWith('.bat')) {
      // On Windows, use exec for batch files to properly handle paths with spaces
      // Quote the command path to handle spaces, then append args
      // Wrap the entire command+args in quotes for cmd /c
      const quotedCommand = `"${command}"`;
      const fullCommand = `cmd /c "${quotedCommand} ${args.join(' ')}"`;
      
      const proc = exec(fullCommand, {
        ...options,
      });

      // Pipe stdout and stderr to parent process
      if (proc.stdout) proc.stdout.pipe(process.stdout);
      if (proc.stderr) proc.stderr.pipe(process.stderr);

      proc.on('close', (code) => {
        if (code === 0) {
          resolve(code);
        } else {
          reject(new Error(`Command failed with exit code ${code}`));
        }
      });

      proc.on('error', (error) => {
        reject(error);
      });
    } else {
      // For non-batch files, use spawn
      const proc = spawn(command, args, {
        ...options,
        stdio: 'inherit',
        shell: false,
      });

      proc.on('close', (code) => {
        if (code === 0) {
          resolve(code);
        } else {
          reject(new Error(`Command failed with exit code ${code}`));
        }
      });

      proc.on('error', (error) => {
        reject(error);
      });
    }
  });
}

async function main() {
  logSection('UE-MCP Build');

  let plan;
  try {
    plan = createTestBuildPlan();
    assertTestProject(plan.projectFile);
  } catch (error) {
    log(`ERROR: ${error.message}`, 'red');
    process.exit(1);
  }

  const {
    projectRoot,
    projectFile,
    engineRoot,
    engineRootSource,
    buildTool,
    buildArgs,
    allowEngineChanges,
    protectedRoots,
  } = plan;

  log(`Project Root: ${projectRoot}`);
  log(`Project File: ${projectFile}`);
  log(`Engine Root: ${engineRoot} (from ${engineRootSource})`);
  log(`Build Tool: ${buildTool}`);
  if (protectedRoots.length > 0) {
    log(`Protected roots: ${protectedRoots.join(', ')}`);
  }
  if (allowEngineChanges) {
    log('Engine changes: ALLOWED via UE_MCP_ALLOW_TEST_ENGINE_CHANGES. This build may write into the engine tree.', 'yellow');
  } else {
    log('Engine changes: blocked (-NoEngineChanges)');
  }
  log('');

  log('Starting build...');
  log(`Command: ${buildTool} ${buildArgs.join(' ')}`);
  log('');

  try {
    // Run the build
    await runCommand(buildTool, buildArgs);
    
    logSection('Build succeeded!');
    process.exit(0);
  } catch (error) {
    logSection('Build failed!');
    log('Check the output above for errors.', 'red');
    process.exit(1);
  }
}

// Run the script
main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
