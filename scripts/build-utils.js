import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// ANSI color codes for better output
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
};

function log(message, color = 'reset') {
  console.log(`${colors[color]}${message}${colors.reset}`);
}

function logSection(message) {
  log('\n' + '='.repeat(32), 'bright');
  log(`   ${message}`, 'bright');
  log('='.repeat(32) + '\n', 'bright');
}

async function fileExists(filePath) {
  try {
    await fs.promises.access(filePath, fs.constants.F_OK);
    return true;
  } catch {
    return false;
  }
}

function envFlag(env, name) {
  return /^(1|true|yes|on)$/i.test(env[name] || '');
}

function pathImpl(platform) {
  return platform === 'win32' ? path.win32 : path.posix;
}

/**
 * Normalize a path for comparison on the target platform. Windows paths are
 * case-insensitive, so they are lowercased; separators are normalized and a
 * trailing separator is dropped so `D:\Engine` and `D:\Engine\` compare equal.
 */
function comparisonPath(value, platform = process.platform) {
  const normalized = pathImpl(platform).normalize(value).replace(/[\\/]+$/, '');
  return platform === 'win32' ? normalized.toLowerCase() : normalized;
}

/**
 * True when `candidate` is `parent` itself or lives beneath it. The trailing
 * separator on the parent is what stops `/ue/engine-test` from being treated
 * as a child of `/ue/engine`.
 */
function isSameOrUnder(candidate, parent, platform = process.platform) {
  const normalizedCandidate = comparisonPath(candidate, platform);
  const normalizedParent = comparisonPath(parent, platform);
  return normalizedCandidate === normalizedParent ||
    normalizedCandidate.startsWith(`${normalizedParent}${pathImpl(platform).sep}`);
}

function engineExecutables(engineRoot, platform = process.platform) {
  const impl = pathImpl(platform);

  if (platform === 'win32') {
    return {
      buildTool: impl.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Build.bat'),
      editorExecutable: impl.join(engineRoot, 'Engine', 'Binaries', 'Win64', 'UnrealEditor.exe'),
    };
  }

  if (platform === 'darwin') {
    return {
      buildTool: impl.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Mac', 'Build.sh'),
      editorExecutable: impl.join(engineRoot, 'Engine', 'Binaries', 'Mac', 'UnrealEditor.app', 'Contents', 'MacOS', 'UnrealEditor'),
    };
  }

  return {
    buildTool: impl.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Linux', 'Build.sh'),
    editorExecutable: impl.join(engineRoot, 'Engine', 'Binaries', 'Linux', 'UnrealEditor'),
  };
}

function unrealTargetPlatform(platform = process.platform) {
  if (platform === 'win32') return 'Win64';
  if (platform === 'darwin') return 'Mac';
  return 'Linux';
}

/**
 * Walk up from a path inside an engine tree to the engine root (the directory
 * that contains `Engine`). Covers the Windows build tool layout
 * (Engine/Build/BatchFiles/Build.bat), the Mac and Linux layouts
 * (Engine/Build/BatchFiles/<Platform>/Build.sh), and editor binaries.
 */
function engineRootFromEnginePath(enginePath, platform = process.platform) {
  const impl = pathImpl(platform);
  let directory = impl.dirname(enginePath);

  while (directory && directory !== impl.dirname(directory)) {
    if (impl.basename(directory).toLowerCase() === 'engine') {
      return impl.dirname(directory);
    }
    directory = impl.dirname(directory);
  }

  return null;
}

function defaultEngineRoots(platform = process.platform) {
  const versions = ['5.8', '5.7', '5.6', '5.5', '5.4', '5.3'];

  if (platform === 'win32') {
    return versions.map((version) => path.win32.join('C:/Program Files/Epic Games', `UE_${version}`));
  }

  if (platform === 'darwin') {
    return versions.map((version) => path.posix.join('/Users/Shared/Epic Games', `UE_${version}`));
  }

  return [];
}

/**
 * Ordered list of candidate engine roots, most explicit first. Each entry
 * carries the name of the thing that produced it so failures can name the
 * setting the caller has to correct.
 */
function engineRootCandidates(env = process.env, platform = process.platform) {
  const candidates = [];

  if (env.UE_MCP_TEST_ENGINE_ROOT) {
    candidates.push({ source: 'UE_MCP_TEST_ENGINE_ROOT', engineRoot: env.UE_MCP_TEST_ENGINE_ROOT });
  }

  if (env.UE_BUILD_TOOL_PATH) {
    const derived = engineRootFromEnginePath(env.UE_BUILD_TOOL_PATH, platform);
    if (derived) {
      candidates.push({ source: 'UE_BUILD_TOOL_PATH', engineRoot: derived });
    }
  }

  for (const engineRoot of defaultEngineRoots(platform)) {
    candidates.push({ source: 'default engine install', engineRoot });
  }

  return candidates;
}

function canonicalDirectory(directoryPath) {
  try {
    return fs.realpathSync.native(directoryPath);
  } catch {
    return path.resolve(directoryPath);
  }
}

/**
 * Roots the build must never touch. Listing a root here is a hard deny: it wins
 * over every other setting, including UE_MCP_ALLOW_TEST_ENGINE_CHANGES.
 *
 * A relative entry is rejected rather than resolved against the current working
 * directory. Silently turning a mistyped deny entry into a path that can never
 * match would leave the caller believing an engine is protected when it is not.
 */
function protectedEngineRoots(env = process.env) {
  return (env.UE_MCP_PROTECTED_ENGINE_ROOTS || '')
    .split(path.delimiter)
    .map((entry) => entry.trim())
    .filter(Boolean)
    .map((entry) => {
      if (!path.isAbsolute(entry)) {
        throw new Error(
          `UE_MCP_PROTECTED_ENGINE_ROOTS entry '${entry}' is not an absolute path, so it would protect nothing.`
        );
      }
      return canonicalDirectory(entry);
    });
}

function assertNotProtected(engineRoot, roots, platform = process.platform) {
  const match = roots.find((root) => isSameOrUnder(engineRoot, root, platform));
  if (match) {
    throw new Error(
      `Engine root '${engineRoot}' is listed in UE_MCP_PROTECTED_ENGINE_ROOTS ('${match}'). ` +
      'UE-MCP test builds refuse to run against a protected engine.'
    );
  }
}

/**
 * Pick the engine the test project builds and runs against.
 *
 * Resolution never silently falls back to "no engine": if nothing usable is
 * found the caller gets an error naming every location that was tried.
 */
function resolveTestEngine(env = process.env, platform = process.platform) {
  const roots = protectedEngineRoots(env);
  const candidates = engineRootCandidates(env, platform);
  const tried = [];

  for (const candidate of candidates) {
    const engineRoot = canonicalDirectory(candidate.engineRoot);
    const executables = engineExecutables(engineRoot, platform);

    if (!fs.existsSync(executables.buildTool)) {
      tried.push(`${executables.buildTool} (from ${candidate.source})`);

      // An explicitly configured root that does not exist is a mistake worth
      // reporting on its own rather than quietly falling through to a default
      // install the caller did not ask for.
      if (candidate.source !== 'default engine install') {
        throw new Error(
          `${candidate.source} does not point at a usable Unreal engine: ${executables.buildTool} is missing.`
        );
      }
      continue;
    }

    assertNotProtected(engineRoot, roots, platform);

    return {
      engineRoot,
      engineRootSource: candidate.source,
      protectedRoots: roots,
      allowEngineChanges: envFlag(env, 'UE_MCP_ALLOW_TEST_ENGINE_CHANGES'),
      ...executables,
    };
  }

  throw new Error(
    'Unreal Engine build tool not found. Set UE_MCP_TEST_ENGINE_ROOT to a dedicated engine root ' +
    '(or UE_BUILD_TOOL_PATH to a Build.bat / Build.sh), or install UE 5.3+ to a default location. Tried:\n  ' +
    (tried.join('\n  ') || '(no candidate locations)')
  );
}

/**
 * Everything scripts/build.js needs to spawn Unreal, with the safety rails
 * already applied: a fixed target, the repo's own test project, and
 * `-NoEngineChanges` so Unreal itself aborts the build if it would overwrite
 * an existing file under the engine tree.
 */
function createTestBuildPlan(env = process.env, platform = process.platform) {
  const engine = resolveTestEngine(env, platform);
  const { projectRoot, projectFile } = getProjectPaths();
  const buildArgs = [
    'ue_mcpEditor',
    unrealTargetPlatform(platform),
    'Development',
    `-Project="${projectFile}"`,
    '-WaitMutex',
    '-FromMsBuild',
  ];

  if (!engine.allowEngineChanges) {
    buildArgs.push('-NoEngineChanges');
  }

  return { ...engine, projectRoot, projectFile, buildArgs };
}

function getProjectPaths() {
  const projectRoot = path.resolve(__dirname, '..', 'tests', 'ue_mcp');
  const projectFile = path.join(projectRoot, 'ue_mcp.uproject');
  return { projectRoot, projectFile };
}

/**
 * The build scripts must only ever drive the bundled test project. This asserts
 * that the path they resolved is still the one inside this repository, so a
 * future refactor cannot quietly retarget them at somebody's real game.
 */
function assertTestProject(projectFile) {
  const expected = getProjectPaths().projectFile;
  if (comparisonPath(projectFile) !== comparisonPath(expected)) {
    throw new Error(
      `Refusing to build '${projectFile}'. UE-MCP build scripts only build the bundled test project at '${expected}'.`
    );
  }
  if (!fs.existsSync(expected)) {
    throw new Error(`Test project not found: ${expected}`);
  }
}

export {
  colors,
  log,
  logSection,
  fileExists,
  envFlag,
  comparisonPath,
  engineExecutables,
  engineRootFromEnginePath,
  unrealTargetPlatform,
  isSameOrUnder,
  protectedEngineRoots,
  resolveTestEngine,
  createTestBuildPlan,
  getProjectPaths,
  assertTestProject,
};
