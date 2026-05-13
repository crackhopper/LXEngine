import { defaultRepoRoot, resolveManagerConfig } from "./config.js";
import { EditorClient } from "./editor/editor-client.js";
import { discoverReachableEditorClientConfig } from "./editor/runtime-state.js";
import {
  createMcpHttpServer,
  createResourceHandlers,
  createToolHandlers,
} from "./mcp/server.js";
import { EditorOps } from "./ops/editor-ops.js";
import { WorkspaceOps } from "./ops/workspace-ops.js";
import { ProcessSupervisor } from "./process/process-supervisor.js";
import type { ResourceThresholds } from "./process/resource-guardian.js";

const repoRoot = process.env.LXE_MANAGER_REPO_ROOT ?? defaultRepoRoot();
const runtimeRoot = process.env.LXE_MANAGER_RUNTIME_ROOT ?? repoRoot;
const editorExecutable = process.env.LXE_MANAGER_EDITOR_EXECUTABLE;

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
const editorClientProvider = createEditorClient;
const handlers = createToolHandlers({
  editorOps: new EditorOps(processSupervisor, config),
  editorClientProvider,
  workspaceOps: new WorkspaceOps(processSupervisor, { repoRoot: config.repoRoot }),
});
const resources = createResourceHandlers(editorClientProvider);
const port = readPort(process.env.LXE_MANAGER_PORT, 3880);
const server = createMcpHttpServer({
  handlers,
  resources,
  bearerToken: process.env.LXE_MANAGER_MCP_BEARER_TOKEN,
});

server.listen(port, "127.0.0.1", () => {
  console.log(
    JSON.stringify(
      {
        status: "listening",
        endpoint: `http://127.0.0.1:${port}/mcp`,
        config,
        editorApi: "dynamic",
        tools: Object.keys(handlers).sort(),
      },
      null,
      2,
    ),
  );
});

async function createEditorClient(): Promise<EditorClient | undefined> {
  if (process.env.LXE_EDITOR_HTTP_BASE_URL) {
    return new EditorClient({
      httpBaseUrl: process.env.LXE_EDITOR_HTTP_BASE_URL,
      bearerToken: process.env.LXE_EDITOR_API_TOKEN,
    });
  }

  const discovered = await discoverReachableEditorClientConfig(config.runtimeStatePath);
  if (!discovered) {
    return undefined;
  }

  return new EditorClient({
    httpBaseUrl: discovered.httpBaseUrl,
    bearerToken: discovered.bearerToken,
  });
}

function readPort(value: string | undefined, fallback: number): number {
  if (value === undefined) {
    return fallback;
  }
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65_535) {
    throw new Error(`invalid LXE_MANAGER_PORT: ${value}`);
  }
  return port;
}
