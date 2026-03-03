import { ProjectContext } from "./dist/project.js";
import { deploy, deploySummary } from "./dist/deployer.js";

const projectPath = process.argv[2] || "C:\\Users\\david\\Projects\\UE\\ue-mcp\\tests\\ue_mcp\\ue_mcp.uproject";

const project = new ProjectContext();
project.setProject(projectPath);

console.log(`Deploying bridge to: ${project.projectPath}`);
const result = deploy(project);
console.log(deploySummary(result));

if (result.error) {
  console.error(`Error: ${result.error}`);
  process.exit(1);
}

console.log("\nDeployment complete! Restart the UE editor for the bridge to start automatically.");
