/**
 * Find the build a pull request's CI made of the tree being published, so the publish job ships
 * those exact files instead of installing and compiling again.
 *
 *   node scripts/tested-build.mjs --repo owner/name --tree <tree sha>   >> "$GITHUB_OUTPUT"
 *
 * The package job uploads dist/ as `tested-build-<tree>`, keyed by the git tree it built rather than
 * the commit: the merge commit on main differs from the pull request's head, but main only takes a
 * pull request that is up to date with it, so the two have one tree. A build qualifies when it came
 * from a successful pull_request run of ci.yml in this repository, not a fork, and the head commit of
 * that run has the same tree. Prints `found=true` and `run_id=<id>`, or `found=false` when none
 * qualifies, in which case the publish job builds the tree itself.
 */
import { execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";

export const ARTIFACT_PREFIX = "tested-build-";
export const WORKFLOW_PATH = ".github/workflows/ci.yml";

/**
 * The newest qualifying candidate, and why each newer one did not qualify.
 * A candidate: { artifactId, expired, createdAt, runId, repositoryId, headRepositoryId, run, headTree },
 * where run is { event, conclusion, path } and headTree the tree of the run's head commit.
 */
export function pickTestedBuild(tree, candidates) {
  const rejected = [];
  const sorted = [...candidates].sort((a, b) => (a.createdAt < b.createdAt ? 1 : a.createdAt > b.createdAt ? -1 : 0));
  for (const c of sorted) {
    const why =
      c.expired ? "expired"
      : c.headRepositoryId !== c.repositoryId ? "built for a fork"
      : c.run?.event !== "pull_request" ? `from a ${c.run?.event ?? "unknown"} run`
      : c.run?.path !== WORKFLOW_PATH ? `from ${c.run?.path ?? "an unknown workflow"}`
      : c.run?.conclusion !== "success" ? `its run concluded ${c.run?.conclusion ?? "nothing yet"}`
      : c.headTree !== tree ? `its head commit has tree ${c.headTree}`
      : null;
    if (!why) return { pick: c, rejected };
    rejected.push({ runId: c.runId, why });
  }
  return { pick: null, rejected };
}

function gh(path) {
  return JSON.parse(execFileSync("gh", ["api", path], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 }));
}

function main(argv) {
  const flag = (n) => {
    const i = argv.indexOf(n);
    return i === -1 ? null : argv[i + 1];
  };
  const repo = flag("--repo");
  const tree = flag("--tree");
  if (!repo || !/^[0-9a-f]{40}$/.test(tree ?? "")) {
    console.error("usage: node scripts/tested-build.mjs --repo owner/name --tree <40-hex tree sha>");
    return 2;
  }
  const name = `${ARTIFACT_PREFIX}${tree}`;
  const { artifacts = [] } = gh(`repos/${repo}/actions/artifacts?name=${name}&per_page=100`);
  const candidates = artifacts.filter((a) => a.name === name).map((a) => {
    const runId = a.workflow_run?.id;
    const headSha = a.workflow_run?.head_sha;
    let run = null;
    let headTree = null;
    try {
      if (runId) run = gh(`repos/${repo}/actions/runs/${runId}`);
      if (headSha) headTree = gh(`repos/${repo}/git/commits/${headSha}`).tree?.sha ?? null;
    } catch (e) {
      console.error(`tested-build: could not read run ${runId}: ${e.message}`);
    }
    return {
      artifactId: a.id,
      expired: a.expired,
      createdAt: a.created_at,
      runId,
      repositoryId: a.workflow_run?.repository_id,
      headRepositoryId: a.workflow_run?.head_repository_id,
      run: run && { event: run.event, conclusion: run.conclusion, path: run.path },
      headTree,
    };
  });
  const { pick, rejected } = pickTestedBuild(tree, candidates);
  for (const r of rejected) console.error(`tested-build: run ${r.runId} skipped, ${r.why}`);
  if (!pick) {
    console.error(`tested-build: no pull request run built tree ${tree}; the publish job builds it`);
    console.log("found=false");
    return 0;
  }
  console.error(`tested-build: tree ${tree} was built and tested by run ${pick.runId}`);
  console.log("found=true");
  console.log(`run_id=${pick.runId}`);
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exit(main(process.argv.slice(2)));
}
