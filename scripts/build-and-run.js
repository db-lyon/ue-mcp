#!/usr/bin/env node

import { execSync } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { log, logSection } from './build-utils.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/**
 * Ask THIS project's editor to close, over its own bridge.
 *
 * lint-prose-allow: no-kill  naming what this replaced
 * The previous version ran `taskkill /IM UnrealEditor.exe`, which has two
 * problems. It matches every Unreal editor on the machine, so building the
 * test project closed the editor somebody had open on their real game. And it
 * terminates a process the user is running rather than asking it to stop: the
 * editor never gets to raise its save prompt, and anything unsaved is gone.
 *
 * The bridge already has a shutdown that does this properly. It is addressed
 * to one project through that project's own port lockfile, it refuses while
 * there is unsaved work, and it never kills anything. If the editor will not
 * close, that is reported and the build stops, because the alternative is
 * deciding on the user's behalf that their unsaved work does not matter.
 */
async function requestEditorShutdown() {
  const {
    bridgePortCandidates,
    askOnce,
    isTestProjectDir,
    extractReportedProjectDir,
    PROJECT_IDENTITY_PYTHON,
    TEST_PROJECT_DIR,
  } = await import("./bridge-target.mjs");
  const { candidates } = bridgePortCandidates({ projectDir: TEST_PROJECT_DIR });

  // Every candidate is asked WHICH PROJECT it has open before it is asked to
  // close anything. The candidate list includes a port derived from the path
  // and the legacy fixed port, so "something answered" is not evidence that
  // the thing answering is this project's editor. Without the challenge this
  // sends a shutdown to whatever is listening, which is the same blast radius
  // as killing every editor by process name.
  const ours = [];
  for (const candidate of candidates) {
    const url = `ws://127.0.0.1:${candidate.port}`;
    let reported;
    try {
      await askOnce(url, "get_bridge_capabilities", {}, 2000);
      const identity = await askOnce(url, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, 10000);
      reported = extractReportedProjectDir(identity);
    } catch {
      // Nothing listening, or it would not say. Either way it is not ours.
      continue;
    }
    if (isTestProjectDir(reported)) {
      ours.push(url);
    } else {
      log(`  Leaving the editor on ${url} alone: it has ${reported ?? "an unknown project"} open.`, 'yellow');
    }
  }
  if (ours.length === 0) return true;

  log('Asking the Unreal Editor to close (save your work if prompted)...', 'yellow');
  for (const url of ours) {
    let reply;
    try {
      reply = await askOnce(url, "request_editor_shutdown", {}, 15000);
    } catch (e) {
      log(`  The editor did not answer: ${e.message}`, 'yellow');
      continue;
    }
    // A refusal comes back INSIDE the result, not as a transport error, so a
    // catch alone never sees it: the reason and the list of unsaved packages
    // were both discarded and the build waited thirty seconds in silence.
    if (reply && reply.success === false) {
      log(`  The editor declined to close: ${reply.error ?? reply.message ?? "no reason given"}`, 'yellow');
      const dirty = reply.dirtyPackages ?? reply.packages ?? reply.content;
      if (Array.isArray(dirty) && dirty.length > 0) {
        log(`  Unsaved: ${dirty.map((d) => d.package ?? d).join(", ")}`, 'yellow');
      }
      return false;
    }
  }

  for (let i = 0; i < 30; i++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    let stillUp = false;
    for (const url of ours) {
      try {
        await askOnce(url, "get_bridge_capabilities", {}, 1000);
        stillUp = true;
      } catch {
        // This one has gone.
      }
    }
    if (!stillUp) {
      log('Unreal Editor closed', 'yellow');
      return true;
    }
  }

  log('Editor still running, so the build stopped. Close it yourself and try again.', 'red');
  return false;
}

async function main() {
  logSection('UE-MCP Build and Run');

  const buildScript = path.join(__dirname, 'build.js');
  const runScript = path.join(__dirname, 'run.js');

  try {
    // The editor holds the module DLLs, so it has to be down before a build
    // can link. Asked, never killed.
    if (!(await requestEditorShutdown())) {
      process.exit(1);
    }
    
    // Run build script
    log('Running build...', 'green');
    execSync(`node "${buildScript}"`, { stdio: 'inherit' });
    
    // If build succeeded, run the project
    log('');
    log('Running project...', 'green');
    execSync(`node "${runScript}"`, { stdio: 'inherit' });
    
    process.exit(0);
  } catch (error) {
    // Error output is already handled by the individual scripts
    process.exit(1);
  }
}

// Run the script
main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
