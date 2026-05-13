import { resolveManagerConfig } from "./config.js";

const repoRoot = process.env.LXE_MANAGER_REPO_ROOT ?? process.cwd();
const runtimeRoot = process.env.LXE_MANAGER_RUNTIME_ROOT ?? repoRoot;

const config = resolveManagerConfig({ repoRoot, runtimeRoot });

console.log(
  JSON.stringify(
    {
      status: "starting",
      config,
    },
    null,
    2,
  ),
);
