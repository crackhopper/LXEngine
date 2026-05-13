import { defaultRepoRoot, resolveManagerConfig } from "./config.js";
import { EditorClient } from "./editor/editor-client.js";
import { createToolHandlers } from "./mcp/server.js";
import { EditorOps } from "./ops/editor-ops.js";
import { WorkspaceOps } from "./ops/workspace-ops.js";
import { ProcessSupervisor } from "./process/process-supervisor.js";
import type { ResourceThresholds } from "./process/resource-guardian.js";

const repoRoot = process.env.LXE_MANAGER_REPO_ROOT ?? defaultRepoRoot();
const runtimeRoot = process.env.LXE_MANAGER_RUNTIME_ROOT ?? repoRoot;
const editorExecutable = process.env.LXE_MANAGER_EDITOR_EXECUTABLE;
const editorHttpBaseUrl =
  process.env.LXE_EDITOR_HTTP_BASE_URL ?? "http://127.0.0.1:3768";

const config = resolveManagerConfig({ repoRoot, runtimeRoot, editorExecutable });
const defaultResourceThresholds: ResourceThresholds = {
  minSystemFreeMemoryBytes: 512 * 1024 * 1024,
  maxProcessRssBytes: 8 * 1024 * 1024 * 1024,
  maxProcessCpuPercent: Number.POSITIVE_INFINITY,
  maxSystemCpuBusyPercent: Number.POSITIVE_INFINITY,
  maxProcessIoBytesPerSecond: Number.POSITIVE_INFINITY,
  maxSystemIoBusyPercent: Number.POSITIVE_INFINITY,
};
const processSupervisor = new ProcessSupervisor({
  defaultResourceThresholds,
});
const handlers = createToolHandlers({
  editorOps: new EditorOps(processSupervisor, config),
  editorClient: new EditorClient({ httpBaseUrl: editorHttpBaseUrl }),
  workspaceOps: new WorkspaceOps(processSupervisor, { repoRoot: config.repoRoot }),
});

console.log(
  JSON.stringify(
    {
      status: "starting",
      config,
      tools: Object.keys(handlers),
    },
    null,
    2,
  ),
);
