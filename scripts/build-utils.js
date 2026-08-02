import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const TEST_ENGINE_MARKER = '.ue-mcp-test-engine';

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

function canonicalDirectory(directoryPath, variableName) {
  if (!path.isAbsolute(directoryPath)) {
    throw new Error(`${variableName} must be an absolute path.`);
  }

  try {
    return fs.realpathSync.native(directoryPath);
  } catch {
    throw new Error(`${variableName} does not identify an existing directory: ${directoryPath}`);
  }
}

function comparisonPath(value, platform = process.platform) {
  const normalized = path.normalize(value).replace(/[\\/]+$/, '');
  return platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function isSameOrUnder(candidate, parent, platform = process.platform) {
  const normalizedCandidate = comparisonPath(candidate, platform);
  const normalizedParent = comparisonPath(parent, platform);
  return normalizedCandidate === normalizedParent ||
    normalizedCandidate.startsWith(`${normalizedParent}${path.sep}`);
}

function engineExecutables(engineRoot, platform = process.platform) {
  if (platform === 'win32') {
    return {
      buildTool: path.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Build.bat'),
      editorExecutable: path.join(engineRoot, 'Engine', 'Binaries', 'Win64', 'UnrealEditor.exe'),
    };
  }

  if (platform === 'darwin') {
    return {
      buildTool: path.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Mac', 'Build.sh'),
      editorExecutable: path.join(engineRoot, 'Engine', 'Binaries', 'Mac', 'UnrealEditor.app', 'Contents', 'MacOS', 'UnrealEditor'),
    };
  }

  return {
    buildTool: path.join(engineRoot, 'Engine', 'Build', 'BatchFiles', 'Linux', 'Build.sh'),
    editorExecutable: path.join(engineRoot, 'Engine', 'Binaries', 'Linux', 'UnrealEditor'),
  };
}

function unrealTargetPlatform(platform = process.platform) {
  if (platform === 'win32') return 'Win64';
  if (platform === 'darwin') return 'Mac';
  return 'Linux';
}

function resolveTestEngine(env = process.env, platform = process.platform) {
  const configuredRoot = env.UE_MCP_TEST_ENGINE_ROOT;
  if (!configuredRoot) {
    throw new Error('UE_MCP_TEST_ENGINE_ROOT is required. Use a dedicated engine root for UE-MCP tests.');
  }

  const engineRoot = canonicalDirectory(configuredRoot, 'UE_MCP_TEST_ENGINE_ROOT');
  const markerPath = path.join(engineRoot, TEST_ENGINE_MARKER);
  if (!fs.existsSync(markerPath)) {
    throw new Error(`UE_MCP_TEST_ENGINE_ROOT is not marked as a dedicated test engine. Missing: ${markerPath}`);
  }

  const protectedRoots = (env.UE_MCP_PROTECTED_ENGINE_ROOTS || '')
    .split(path.delimiter)
    .filter(Boolean)
    .map((protectedRoot) => canonicalDirectory(protectedRoot, 'UE_MCP_PROTECTED_ENGINE_ROOTS'));

  const protectedRoot = protectedRoots.find((root) => isSameOrUnder(engineRoot, root, platform));
  if (protectedRoot) {
    throw new Error(`UE-MCP test builds are forbidden under protected engine root: ${protectedRoot}`);
  }

  const executables = engineExecutables(engineRoot, platform);
  if (!fs.existsSync(executables.buildTool)) {
    throw new Error(`Unreal build tool not found in UE_MCP_TEST_ENGINE_ROOT: ${executables.buildTool}`);
  }

  return {
    engineRoot,
    protectedRoots,
    allowEngineChanges: envFlag(env, 'UE_MCP_ALLOW_TEST_ENGINE_CHANGES'),
    ...executables,
  };
}

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

export {
  colors,
  TEST_ENGINE_MARKER,
  log,
  logSection,
  fileExists,
  envFlag,
  engineExecutables,
  unrealTargetPlatform,
  isSameOrUnder,
  resolveTestEngine,
  createTestBuildPlan,
  getProjectPaths,
};
