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

function findUEBuildTool() {
  // Check for environment variable override first
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) {
    return envPath;
  }

  // Check default UE5 installation paths
  const versions = ['5.7', '5.6', '5.5', '5.4', '5.3'];
  const basePath = 'C:/Program Files/Epic Games';
  
  for (const version of versions) {
    const buildToolPath = path.join(basePath, `UE_${version}`, 'Engine', 'Build', 'BatchFiles', 'Build.bat');
    if (fs.existsSync(buildToolPath)) {
      return buildToolPath;
    }
  }

  return null;
}

function getProjectPaths() {
  const projectRoot = path.resolve(__dirname, '..', 'tests', 'ue_mcp');
  const projectFile = path.join(projectRoot, 'ue_mcp.uproject');
  return { projectRoot, projectFile };
}

export {
  colors,
  log,
  logSection,
  fileExists,
  findUEBuildTool,
  getProjectPaths,
};
