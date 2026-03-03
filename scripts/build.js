#!/usr/bin/env node

import path from 'path';
import { spawn, exec } from 'child_process';
import { promisify } from 'util';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { log, logSection, fileExists, findUEBuildTool, getProjectPaths } from './build-utils.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const execPromise = promisify(exec);

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

  const { projectRoot, projectFile } = getProjectPaths();

  // Check if project file exists
  if (!(await fileExists(projectFile))) {
    log(`ERROR: Project file not found at ${projectFile}`, 'red');
    process.exit(1);
  }

  // Find UE5 build tool
  const buildTool = findUEBuildTool();
  
  if (!buildTool) {
    log('ERROR: Unreal Engine build tool not found!', 'red');
    log('');
    log('Please either:');
    log('  1. Install UE5.3+ to default location, OR');
    log('  2. Set UE_BUILD_TOOL_PATH environment variable to your Build.bat path');
    log('');
    log('Example: set UE_BUILD_TOOL_PATH=C:\\Program Files\\Epic Games\\UE_5.7\\Engine\\Build\\BatchFiles\\Build.bat');
    process.exit(1);
  }

  log(`Project Root: ${projectRoot}`);
  log(`Project File: ${projectFile}`);
  log(`Build Tool: ${buildTool}`);
  log('');

  // Build command arguments
  const buildArgs = [
    'ue_mcpEditor',
    'Win64',
    'Development',
    `-Project="${projectFile}"`,
    '-WaitMutex',
    '-FromMsBuild',
  ];

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
