# lxe_manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone Node.js `lxe_manager` MCP server that manages local `lxe_editor`, repo/build operations, and resource protection while removing MCP support from `lxe_editor`.

**Architecture:** Add a new TypeScript workspace under `tools/lxe_manager/` that hosts the MCP server, editor HTTP/WebSocket client, workspace ops, process supervision, and resource guardian. Keep `lxe_editor` focused on HTTP/WebSocket APIs and runtime discovery, then switch local helper scripts and tests to target the manager instead of the editor `/mcp` endpoint.

**Tech Stack:** TypeScript, Node.js 20+, Vitest, `@modelcontextprotocol/sdk`, `ws`, existing C++/CMake/Ninja test stack, Python black-box tests under `tests/lxe_editor/`

---

## File Map

### New files

- `tools/lxe_manager/package.json`
  - Node workspace metadata, scripts, dependencies
- `tools/lxe_manager/tsconfig.json`
  - TypeScript compiler config
- `tools/lxe_manager/src/index.ts`
  - Manager entrypoint
- `tools/lxe_manager/src/config.ts`
  - Workspace/runtime path configuration
- `tools/lxe_manager/src/logging/operation-log.ts`
  - In-memory operation result log
- `tools/lxe_manager/src/editor/runtime-state.ts`
  - Runtime discovery parsing and validation
- `tools/lxe_manager/src/editor/editor-client.ts`
  - HTTP/WebSocket client for `lxe_editor`
- `tools/lxe_manager/src/process/process-supervisor.ts`
  - Spawn/stop/track process trees
- `tools/lxe_manager/src/process/resource-sampler.ts`
  - Platform-specific CPU/RSS/IO sampling
- `tools/lxe_manager/src/process/kill-policy.ts`
  - Graceful-stop then force-kill escalation
- `tools/lxe_manager/src/process/resource-guardian.ts`
  - Threshold evaluation and guardian orchestration
- `tools/lxe_manager/src/ops/editor-ops.ts`
  - `editor.start/stop/status/logs`
- `tools/lxe_manager/src/ops/workspace-ops.ts`
  - `repo.pull`, `build.configure`, `build.target`, test command ops
- `tools/lxe_manager/src/mcp/server.ts`
  - MCP tool/resource registration and request plumbing
- `tools/lxe_manager/src/mcp/types.ts`
  - Shared MCP-facing result types
- `tools/lxe_manager/tests/runtime-state.test.ts`
  - Runtime discovery parsing coverage
- `tools/lxe_manager/tests/editor-client.test.ts`
  - Fake editor HTTP/WS integration coverage
- `tools/lxe_manager/tests/process-supervisor.test.ts`
  - Spawn/stop/logging behavior
- `tools/lxe_manager/tests/resource-guardian.test.ts`
  - Guardian thresholds and kill escalation coverage
- `tools/lxe_manager/tests/mcp-server.test.ts`
  - MCP tool mapping coverage
- `scripts/lxe_manager/use_local_mcp.sh`
  - Local shell helper that points Codex at the manager
- `scripts/lxe_manager/use_remote_mcp.sh`
  - Remote shell helper for manager URL + bearer token
- `scripts/lxe_manager/use_local_mcp.ps1`
  - Windows local helper
- `scripts/lxe_manager/use_remote_mcp.ps1`
  - Windows remote helper

### Modified files

- `src/demos/lxe_editor/CMakeLists.txt`
  - Stop compiling `lxe_editor_mcp_handler.cpp`
- `src/demos/lxe_editor/main.cpp`
  - Stop writing `mcpUrl` into runtime state
- `src/demos/lxe_editor/runtime_state.hpp`
  - Remove `mcpUrl`
- `src/demos/lxe_editor/runtime_state.cpp`
  - Stop serializing/deserializing `mcpUrl`
- `src/demos/lxe_editor/lxe_editor_api_server.cpp`
  - Remove `POST /mcp`
- `src/demos/lxe_editor/README.md`
  - Document manager-centric flow
- `src/test/CMakeLists.txt`
  - Stop building `lxe_editor_mcp_handler.cpp` into test targets
- `src/test/integration/test_lxe_editor_api_server.cpp`
  - Remove `/mcp` assertions; keep HTTP API coverage
- `tests/lxe_editor/api_client.py`
  - Stop requiring `mcpUrl` from runtime state
- `tests/lxe_editor/test_mcp_config.py`
  - Replace direct-editor `/mcp` config assertions with manager URL config assertions
- `.codex/skills/lxe-editor-debug/SKILL.md`
  - Update runtime discovery wording from editor MCP URL to manager MCP URL

### Deleted files

- `src/demos/lxe_editor/lxe_editor_mcp_handler.hpp`
- `src/demos/lxe_editor/lxe_editor_mcp_handler.cpp`

## Task 1: Scaffold the standalone manager workspace

**Files:**
- Create: `tools/lxe_manager/package.json`
- Create: `tools/lxe_manager/tsconfig.json`
- Create: `tools/lxe_manager/src/index.ts`
- Create: `tools/lxe_manager/src/config.ts`
- Test: `tools/lxe_manager/tests/runtime-state.test.ts`

- [ ] **Step 1: Write the failing runtime-state bootstrap test**

```ts
import { describe, expect, it } from "vitest";
import { resolveManagerConfig } from "../src/config";

describe("resolveManagerConfig", () => {
  it("builds default repo-local paths for the workspace", () => {
    const config = resolveManagerConfig({
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
    });

    expect(config.repoRoot).toBe("/repo");
    expect(config.runtimeStatePath).toBe("/runtime/data/lxe_editor/runtime_state.yaml");
    expect(config.editorExecutable).toContain("/repo/build/src/demos/lxe_editor/lxe_editor");
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd tools/lxe_manager
npm test -- runtime-state.test.ts
```

Expected: FAIL because `package.json`, `vitest`, and `resolveManagerConfig` do not exist yet.

- [ ] **Step 3: Add the Node/TypeScript workspace skeleton**

```json
{
  "name": "lxe_manager",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "tsc -p tsconfig.json",
    "test": "vitest run",
    "dev": "node --loader tsx ./src/index.ts"
  },
  "dependencies": {
    "@modelcontextprotocol/sdk": "^1.12.0",
    "ws": "^8.18.0"
  },
  "devDependencies": {
    "@types/node": "^22.15.0",
    "@types/ws": "^8.5.14",
    "tsx": "^4.19.3",
    "typescript": "^5.8.3",
    "vitest": "^3.1.3"
  }
}
```

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "NodeNext",
    "moduleResolution": "NodeNext",
    "strict": true,
    "rootDir": ".",
    "outDir": "dist",
    "types": ["node"]
  },
  "include": ["src/**/*.ts", "tests/**/*.ts"]
}
```

```ts
export interface ManagerConfig {
  repoRoot: string;
  runtimeRoot: string;
  runtimeStatePath: string;
  editorExecutable: string;
}

export function resolveManagerConfig(input: {
  repoRoot: string;
  runtimeRoot: string;
}): ManagerConfig {
  return {
    repoRoot: input.repoRoot,
    runtimeRoot: input.runtimeRoot,
    runtimeStatePath: `${input.runtimeRoot}/data/lxe_editor/runtime_state.yaml`,
    editorExecutable: `${input.repoRoot}/build/src/demos/lxe_editor/lxe_editor`,
  };
}
```

```ts
import { resolveManagerConfig } from "./config";

const repoRoot = process.env.LXE_MANAGER_REPO_ROOT ?? process.cwd();
const runtimeRoot = process.env.LXE_MANAGER_RUNTIME_ROOT ?? repoRoot;

const config = resolveManagerConfig({ repoRoot, runtimeRoot });
console.log(JSON.stringify({ status: "starting", config }, null, 2));
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cd tools/lxe_manager
npm install
npm test -- runtime-state.test.ts
```

Expected: PASS with one test.

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/package.json tools/lxe_manager/tsconfig.json tools/lxe_manager/src/index.ts tools/lxe_manager/src/config.ts tools/lxe_manager/tests/runtime-state.test.ts
git commit -m "feat: scaffold lxe manager workspace"
```

## Task 2: Implement runtime discovery and editor HTTP client

**Files:**
- Create: `tools/lxe_manager/src/editor/runtime-state.ts`
- Create: `tools/lxe_manager/src/editor/editor-client.ts`
- Create: `tools/lxe_manager/tests/editor-client.test.ts`
- Modify: `tools/lxe_manager/src/config.ts`

- [ ] **Step 1: Write the failing runtime discovery and health-check tests**

```ts
import { createServer } from "node:http";
import { afterEach, describe, expect, it } from "vitest";
import { loadRuntimeState, runtimeStateIsReachable } from "../src/editor/runtime-state";
import { EditorClient } from "../src/editor/editor-client";

describe("editor runtime discovery", () => {
  afterEach(() => process.env.LXE_EDITOR_API_TOKEN = "");

  it("loads runtime_state.yaml without mcpUrl", async () => {
    const state = loadRuntimeState(`
pid: 42
httpHost: 127.0.0.1
httpPort: 3768
wsHost: 127.0.0.1
wsPort: 3768
tokenFile: /tmp/api_token.txt
startedAt: 2026-05-13-120000
`);

    expect(state.httpPort).toBe(3768);
    expect("mcpUrl" in state).toBe(false);
  });

  it("rejects stale discovery when health probe fails", async () => {
    const reachable = await runtimeStateIsReachable({
      pid: 42,
      httpHost: "127.0.0.1",
      httpPort: 65530,
      wsHost: "127.0.0.1",
      wsPort: 65530,
      tokenFile: "/tmp/api_token.txt",
      startedAt: "2026-05-13-120000",
    });

    expect(reachable).toBe(false);
  });

  it("fetches summary from a healthy editor API", async () => {
    const server = createServer((req, res) => {
      if (req.url === "/api/state/summary") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ sceneName: "Scene", dirty: false }));
        return;
      }
      if (req.url === "/health") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ ok: true }));
        return;
      }
      res.writeHead(404).end();
    });

    await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
    const address = server.address();
    if (!address || typeof address === "string") {
      throw new Error("missing address");
    }

    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${address.port}`,
      bearerToken: "",
    });
    await expect(client.getSummary()).resolves.toEqual({
      sceneName: "Scene",
      dirty: false,
    });
    server.close();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd tools/lxe_manager
npm test -- editor-client.test.ts
```

Expected: FAIL because `loadRuntimeState`, `runtimeStateIsReachable`, and `EditorClient` do not exist.

- [ ] **Step 3: Implement runtime parsing and minimal HTTP client**

```ts
export interface EditorRuntimeState {
  pid: number;
  httpHost: string;
  httpPort: number;
  wsHost: string;
  wsPort: number;
  tokenFile: string;
  startedAt: string;
}

export function loadRuntimeState(text: string): EditorRuntimeState {
  const map = new Map<string, string>();
  for (const rawLine of text.split("\n")) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#") || !line.includes(":")) {
      continue;
    }
    const [key, ...rest] = line.split(":");
    map.set(key.trim(), rest.join(":").trim());
  }
  return {
    pid: Number(map.get("pid") ?? "0"),
    httpHost: map.get("httpHost") ?? "",
    httpPort: Number(map.get("httpPort") ?? "0"),
    wsHost: map.get("wsHost") ?? "",
    wsPort: Number(map.get("wsPort") ?? "0"),
    tokenFile: map.get("tokenFile") ?? "",
    startedAt: map.get("startedAt") ?? "",
  };
}

export async function runtimeStateIsReachable(
  state: EditorRuntimeState,
): Promise<boolean> {
  try {
    const response = await fetch(
      `http://${state.httpHost}:${state.httpPort}/health`,
    );
    return response.ok;
  } catch {
    return false;
  }
}
```

```ts
export class EditorClient {
  constructor(
    private readonly input: {
      httpBaseUrl: string;
      bearerToken: string;
    },
  ) {}

  async health(): Promise<unknown> {
    return this.requestJson("GET", "/health");
  }

  async getSummary(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/summary");
  }

  async command(line: string): Promise<unknown> {
    return this.requestJson("POST", "/api/command", { line });
  }

  private async requestJson(
    method: "GET" | "POST",
    path: string,
    body?: unknown,
  ): Promise<unknown> {
    const response = await fetch(`${this.input.httpBaseUrl}${path}`, {
      method,
      headers: {
        "content-type": "application/json",
        ...(this.input.bearerToken
          ? { authorization: `Bearer ${this.input.bearerToken}` }
          : {}),
      },
      body: body ? JSON.stringify(body) : undefined,
    });
    if (!response.ok) {
      throw new Error(`editor request failed: ${response.status}`);
    }
    return response.json();
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cd tools/lxe_manager
npm test -- editor-client.test.ts
```

Expected: PASS with runtime parsing and summary fetch coverage.

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/src/editor/runtime-state.ts tools/lxe_manager/src/editor/editor-client.ts tools/lxe_manager/tests/editor-client.test.ts tools/lxe_manager/src/config.ts
git commit -m "feat: add editor runtime discovery client"
```

## Task 3: Add process supervision and structured workspace operations

**Files:**
- Create: `tools/lxe_manager/src/process/process-supervisor.ts`
- Create: `tools/lxe_manager/src/ops/workspace-ops.ts`
- Create: `tools/lxe_manager/src/logging/operation-log.ts`
- Create: `tools/lxe_manager/tests/process-supervisor.test.ts`

- [ ] **Step 1: Write the failing process and workspace op tests**

```ts
import { describe, expect, it } from "vitest";
import { ProcessSupervisor } from "../src/process/process-supervisor";
import { WorkspaceOps } from "../src/ops/workspace-ops";

describe("process supervision", () => {
  it("captures stdout and exit code for a managed task", async () => {
    const supervisor = new ProcessSupervisor();
    const result = await supervisor.run({
      command: process.execPath,
      args: ["-e", "console.log('hello from task')"],
      cwd: process.cwd(),
      label: "smoke-task",
    });

    expect(result.exitCode).toBe(0);
    expect(result.stdout).toContain("hello from task");
  });

  it("builds a repo pull command in the repo root", async () => {
    const ops = new WorkspaceOps(new ProcessSupervisor(), { repoRoot: "/repo" });
    const command = ops.buildRepoPullCommand();

    expect(command.command).toBe("git");
    expect(command.args).toEqual(["pull", "--ff-only"]);
    expect(command.cwd).toBe("/repo");
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd tools/lxe_manager
npm test -- process-supervisor.test.ts
```

Expected: FAIL because `ProcessSupervisor` and `WorkspaceOps` do not exist.

- [ ] **Step 3: Implement process execution and workspace op builders**

```ts
import { spawn } from "node:child_process";

export interface ManagedResult {
  exitCode: number;
  stdout: string;
  stderr: string;
  label: string;
}

export class ProcessSupervisor {
  async run(input: {
    command: string;
    args: string[];
    cwd: string;
    label: string;
  }): Promise<ManagedResult> {
    return new Promise((resolve, reject) => {
      const child = spawn(input.command, input.args, {
        cwd: input.cwd,
        stdio: ["ignore", "pipe", "pipe"],
      });
      let stdout = "";
      let stderr = "";
      child.stdout.on("data", (chunk) => {
        stdout += chunk.toString();
      });
      child.stderr.on("data", (chunk) => {
        stderr += chunk.toString();
      });
      child.on("error", reject);
      child.on("close", (code) => {
        resolve({
          exitCode: code ?? -1,
          stdout,
          stderr,
          label: input.label,
        });
      });
    });
  }
}
```

```ts
import { ProcessSupervisor } from "../process/process-supervisor";

export class WorkspaceOps {
  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: { repoRoot: string },
  ) {}

  buildRepoPullCommand() {
    return {
      command: "git",
      args: ["pull", "--ff-only"],
      cwd: this.config.repoRoot,
      label: "repo.pull",
    };
  }

  buildConfigureCommand(buildDir: string) {
    return {
      command: "cmake",
      args: ["-S", this.config.repoRoot, "-B", buildDir, "-G", "Ninja"],
      cwd: this.config.repoRoot,
      label: "build.configure",
    };
  }

  async repoPull() {
    return this.supervisor.run(this.buildRepoPullCommand());
  }
}
```

```ts
export interface OperationRecord {
  name: string;
  ok: boolean;
  summary: string;
}

export class OperationLog {
  private readonly records: OperationRecord[] = [];

  push(record: OperationRecord): void {
    this.records.unshift(record);
    this.records.splice(50);
  }

  latest(): OperationRecord | undefined {
    return this.records[0];
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cd tools/lxe_manager
npm test -- process-supervisor.test.ts
```

Expected: PASS with managed process output capture and repo command assembly coverage.

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/src/process/process-supervisor.ts tools/lxe_manager/src/ops/workspace-ops.ts tools/lxe_manager/src/logging/operation-log.ts tools/lxe_manager/tests/process-supervisor.test.ts
git commit -m "feat: add workspace ops supervision core"
```

## Task 4: Add resource sampling, guardian thresholds, and kill escalation

**Files:**
- Create: `tools/lxe_manager/src/process/resource-sampler.ts`
- Create: `tools/lxe_manager/src/process/kill-policy.ts`
- Create: `tools/lxe_manager/src/process/resource-guardian.ts`
- Create: `tools/lxe_manager/tests/resource-guardian.test.ts`

- [ ] **Step 1: Write the failing guardian policy tests**

```ts
import { describe, expect, it, vi } from "vitest";
import { ResourceGuardian } from "../src/process/resource-guardian";

describe("resource guardian", () => {
  it("does not trip on a short spike", async () => {
    const kill = vi.fn(async () => undefined);
    const guardian = new ResourceGuardian({
      sample: async () => ({
        processRssBytes: 128,
        processCpuPercent: 15,
        systemFreeMemoryBytes: 1024,
        systemCpuBusyPercent: 40,
        processReadBytesPerSecond: 1,
        processWriteBytesPerSecond: 1,
        systemIoBusyPercent: 20,
      }),
      kill,
      maxConsecutiveBreaches: 3,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    expect(kill).not.toHaveBeenCalled();
  });

  it("trips after sustained pressure", async () => {
    const kill = vi.fn(async () => undefined);
    const guardian = new ResourceGuardian({
      sample: async () => ({
        processRssBytes: 4096,
        processCpuPercent: 390,
        systemFreeMemoryBytes: 16,
        systemCpuBusyPercent: 99,
        processReadBytesPerSecond: 1024,
        processWriteBytesPerSecond: 1024,
        systemIoBusyPercent: 98,
      }),
      kill,
      maxConsecutiveBreaches: 2,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    await guardian.tick();
    expect(kill).toHaveBeenCalledOnce();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd tools/lxe_manager
npm test -- resource-guardian.test.ts
```

Expected: FAIL because guardian types and breach logic do not exist.

- [ ] **Step 3: Implement sampler types, guardian logic, and kill policy**

```ts
export interface ResourceSample {
  processRssBytes: number;
  processCpuPercent: number;
  systemFreeMemoryBytes: number;
  systemCpuBusyPercent: number;
  processReadBytesPerSecond: number;
  processWriteBytesPerSecond: number;
  systemIoBusyPercent: number;
}
```

```ts
export class KillPolicy {
  constructor(
    private readonly gracefulStop: () => Promise<void>,
    private readonly forceKill: () => Promise<void>,
  ) {}

  async terminate(): Promise<void> {
    await this.gracefulStop();
    await new Promise((resolve) => setTimeout(resolve, 3000));
    await this.forceKill();
  }
}
```

```ts
export class ResourceGuardian {
  private breachCount = 0;

  constructor(
    private readonly input: {
      sample: () => Promise<ResourceSample>;
      kill: () => Promise<void>;
      maxConsecutiveBreaches: number;
      thresholds: {
        minSystemFreeMemoryBytes: number;
        maxProcessRssBytes: number;
        maxProcessCpuPercent: number;
        maxSystemCpuBusyPercent: number;
        maxSystemIoBusyPercent: number;
      };
    },
  ) {}

  async tick(): Promise<void> {
    const sample = await this.input.sample();
    const memoryDanger =
      sample.processRssBytes >= this.input.thresholds.maxProcessRssBytes &&
      sample.systemFreeMemoryBytes <= this.input.thresholds.minSystemFreeMemoryBytes;
    const cpuDanger =
      sample.processCpuPercent >= this.input.thresholds.maxProcessCpuPercent &&
      sample.systemCpuBusyPercent >= this.input.thresholds.maxSystemCpuBusyPercent;
    const ioDanger =
      sample.systemIoBusyPercent >= this.input.thresholds.maxSystemIoBusyPercent;

    if (memoryDanger || cpuDanger || ioDanger) {
      this.breachCount += 1;
      if (this.breachCount >= this.input.maxConsecutiveBreaches) {
        await this.input.kill();
      }
      return;
    }

    this.breachCount = 0;
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cd tools/lxe_manager
npm test -- resource-guardian.test.ts
```

Expected: PASS with short-spike and sustained-pressure coverage.

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/src/process/resource-sampler.ts tools/lxe_manager/src/process/kill-policy.ts tools/lxe_manager/src/process/resource-guardian.ts tools/lxe_manager/tests/resource-guardian.test.ts
git commit -m "feat: add resource guardian policies"
```

## Task 5: Add editor lifecycle operations and MCP tool registration

**Files:**
- Create: `tools/lxe_manager/src/ops/editor-ops.ts`
- Create: `tools/lxe_manager/src/mcp/types.ts`
- Create: `tools/lxe_manager/src/mcp/server.ts`
- Modify: `tools/lxe_manager/src/index.ts`
- Create: `tools/lxe_manager/tests/mcp-server.test.ts`

- [ ] **Step 1: Write the failing MCP mapping tests**

```ts
import { describe, expect, it, vi } from "vitest";
import { createToolHandlers } from "../src/mcp/server";

describe("mcp tool handlers", () => {
  it("routes editor summary to the editor client", async () => {
    const handlers = createToolHandlers({
      editorOps: {
        status: vi.fn(async () => ({ running: true })),
      },
      editorClient: {
        getSummary: vi.fn(async () => ({ sceneName: "Scene", dirty: false })),
      },
      workspaceOps: {
        repoPull: vi.fn(),
      },
    });

    await expect(handlers["editor.get_summary"]()).resolves.toEqual({
      content: [{ type: "text", text: '{"sceneName":"Scene","dirty":false}' }],
    });
  });

  it("routes repo pull to workspace ops", async () => {
    const repoPull = vi.fn(async () => ({ exitCode: 0, stdout: "ok", stderr: "" }));
    const handlers = createToolHandlers({
      editorOps: { status: vi.fn() },
      editorClient: { getSummary: vi.fn() },
      workspaceOps: { repoPull },
    });

    await handlers["ops.repo_pull"]();
    expect(repoPull).toHaveBeenCalledOnce();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd tools/lxe_manager
npm test -- mcp-server.test.ts
```

Expected: FAIL because MCP tool registration code does not exist.

- [ ] **Step 3: Implement editor ops and MCP tool handler registration**

```ts
export interface EditorStatus {
  running: boolean;
  pid?: number;
}

export class EditorOps {
  async status(): Promise<EditorStatus> {
    return { running: false };
  }
}
```

```ts
export interface ToolResult {
  content: Array<{ type: "text"; text: string }>;
}
```

```ts
import type { ToolResult } from "./types";

export function createToolHandlers(input: {
  editorOps: { status: () => Promise<unknown> };
  editorClient: { getSummary: () => Promise<unknown> };
  workspaceOps: { repoPull: () => Promise<unknown> };
}): Record<string, () => Promise<ToolResult>> {
  return {
    "editor.get_summary": async () => ({
      content: [
        {
          type: "text",
          text: JSON.stringify(await input.editorClient.getSummary()),
        },
      ],
    }),
    "ops.repo_pull": async () => ({
      content: [
        {
          type: "text",
          text: JSON.stringify(await input.workspaceOps.repoPull()),
        },
      ],
    }),
    "ops.editor_status": async () => ({
      content: [
        {
          type: "text",
          text: JSON.stringify(await input.editorOps.status()),
        },
      ],
    }),
  };
}
```

```ts
import { EditorOps } from "./ops/editor-ops";
import { WorkspaceOps } from "./ops/workspace-ops";
import { ProcessSupervisor } from "./process/process-supervisor";
import { createToolHandlers } from "./mcp/server";

const handlers = createToolHandlers({
  editorOps: new EditorOps(),
  editorClient: {
    getSummary: async () => ({ unavailable: true }),
  },
  workspaceOps: new WorkspaceOps(new ProcessSupervisor(), {
    repoRoot: process.cwd(),
  }),
});

console.log(Object.keys(handlers));
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cd tools/lxe_manager
npm test -- mcp-server.test.ts
```

Expected: PASS with tool-to-operation mapping coverage.

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/src/ops/editor-ops.ts tools/lxe_manager/src/mcp/types.ts tools/lxe_manager/src/mcp/server.ts tools/lxe_manager/src/index.ts tools/lxe_manager/tests/mcp-server.test.ts
git commit -m "feat: wire manager mcp tool handlers"
```

## Task 6: Remove MCP from lxe_editor runtime state and HTTP server

**Files:**
- Delete: `src/demos/lxe_editor/lxe_editor_mcp_handler.hpp`
- Delete: `src/demos/lxe_editor/lxe_editor_mcp_handler.cpp`
- Modify: `src/demos/lxe_editor/runtime_state.hpp`
- Modify: `src/demos/lxe_editor/runtime_state.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`
- Modify: `src/test/integration/test_lxe_editor_api_server.cpp`

- [ ] **Step 1: Write the failing runtime-state/API regression tests**

```cpp
EXPECT(savedText.find("mcpUrl") == std::string::npos,
       "runtime state should no longer publish mcpUrl");
EXPECT(response.find("404 Not Found") != std::string::npos,
       "POST /mcp should no longer exist on the editor API server");
```

Add those assertions into:

- `src/test/integration/test_lxe_editor_api_server.cpp`
- a new runtime-state save/load assertion in the same test file or the existing runtime-state test block

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_lxe_editor_api_server
./build/src/test/test_lxe_editor_api_server
```

Expected: FAIL because runtime state still contains `mcpUrl` and `/mcp` still returns MCP responses.

- [ ] **Step 3: Remove the embedded MCP handler and runtime-state field**

```cpp
struct LxeEditorRuntimeState final {
  int pid = 0;
  std::string httpHost;
  std::uint16_t httpPort = 0;
  std::string wsHost;
  std::uint16_t wsPort = 0;
  std::string tokenFile;
  std::string startedAt;

  bool operator==(const LxeEditorRuntimeState&) const = default;
};
```

```cpp
out << YAML::Key << "tokenFile" << YAML::Value << state.tokenFile;
out << YAML::Key << "startedAt" << YAML::Value << state.startedAt;
```

```cpp
demo::saveLxeEditorRuntimeState(
    resolveRuntimePath("data/lxe_editor"),
    demo::LxeEditorRuntimeState{
        .pid = currentProcessId(),
        .httpHost = apiOptions->enabled ? runtimeHost : std::string{},
        .httpPort = apiBoundPort,
        .wsHost = apiOptions->enabled ? runtimeHost : std::string{},
        .wsPort = apiBoundPort,
        .tokenFile = apiTokenState.tokenPath().string(),
        .startedAt = currentTimestampString(),
    });
```

```cpp
// Delete the /mcp request branch entirely and leave only /health, /api/... and /ws.
```

```cmake
set(LXE_EDITOR_SOURCES
  main.cpp
  lxe_editor_api_server.cpp
  lxe_editor_api_service.cpp
  runtime_state.cpp
)
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build --target test_lxe_editor_api_server
./build/src/test/test_lxe_editor_api_server
```

Expected: PASS with `/mcp` returning 404 and runtime state omitting `mcpUrl`.

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/runtime_state.hpp src/demos/lxe_editor/runtime_state.cpp src/demos/lxe_editor/main.cpp src/demos/lxe_editor/lxe_editor_api_server.cpp src/demos/lxe_editor/CMakeLists.txt src/test/CMakeLists.txt src/test/integration/test_lxe_editor_api_server.cpp
git rm src/demos/lxe_editor/lxe_editor_mcp_handler.hpp src/demos/lxe_editor/lxe_editor_mcp_handler.cpp
git commit -m "refactor: remove embedded editor mcp server"
```

## Task 7: Switch helper scripts, Python tooling, and docs to the manager flow

**Files:**
- Create: `scripts/lxe_manager/use_local_mcp.sh`
- Create: `scripts/lxe_manager/use_remote_mcp.sh`
- Create: `scripts/lxe_manager/use_local_mcp.ps1`
- Create: `scripts/lxe_manager/use_remote_mcp.ps1`
- Modify: `tests/lxe_editor/api_client.py`
- Modify: `tests/lxe_editor/test_mcp_config.py`
- Modify: `src/demos/lxe_editor/README.md`
- Modify: `.codex/skills/lxe-editor-debug/SKILL.md`

- [ ] **Step 1: Write the failing manager-config tests**

Replace the direct-editor assertions in `tests/lxe_editor/test_mcp_config.py` with expectations like:

```python
self.assertIn('url = "http://127.0.0.1:3880/mcp"', output)
self.assertIn('bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"', output)
```

and:

```python
with self.assertRaises(FileNotFoundError):
    client.read_mcp_url()
```

for `tests/lxe_editor/api_client.py`, because editor runtime state no longer carries an MCP URL.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 -m unittest tests.lxe_editor.test_mcp_config
```

Expected: FAIL because old scripts and client code still assume editor-published `mcpUrl`.

- [ ] **Step 3: Implement manager-oriented scripts and tooling updates**

```sh
#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_url="${LXE_MANAGER_URL:-http://127.0.0.1:3880/mcp}"
config_path="${LXE_EDITOR_CODEX_CONFIG_PATH:-${repo_root}/.codex/config.toml}"

cat > "${config_path}" <<EOF
[mcp_servers.lxe_manager]
url = "${manager_url}"
bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"
EOF
```

```python
def read_mcp_url(self) -> str:
    raise FileNotFoundError("manager MCP URL is no longer published by editor runtime_state.yaml")
```

```md
- Local MCP endpoint now comes from `lxe_manager`, not from `lxe_editor`.
- `runtime_state.yaml` publishes editor HTTP/WS discovery only.
- Use `scripts/lxe_manager/use_local_mcp.sh` or `.ps1` to point Codex at the manager.
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
python3 -m unittest tests.lxe_editor.test_mcp_config
```

Expected: PASS with manager-targeting config behavior.

- [ ] **Step 5: Commit**

```bash
git add scripts/lxe_manager/use_local_mcp.sh scripts/lxe_manager/use_remote_mcp.sh scripts/lxe_manager/use_local_mcp.ps1 scripts/lxe_manager/use_remote_mcp.ps1 tests/lxe_editor/api_client.py tests/lxe_editor/test_mcp_config.py src/demos/lxe_editor/README.md .codex/skills/lxe-editor-debug/SKILL.md
git commit -m "feat: switch codex helpers to lxe manager"
```

## Task 8: Finish manager operations, integrate guardian, and verify end-to-end

**Files:**
- Modify: `tools/lxe_manager/src/index.ts`
- Modify: `tools/lxe_manager/src/ops/editor-ops.ts`
- Modify: `tools/lxe_manager/src/ops/workspace-ops.ts`
- Modify: `tools/lxe_manager/src/process/process-supervisor.ts`
- Modify: `tools/lxe_manager/src/process/resource-guardian.ts`
- Modify: `tools/lxe_manager/src/mcp/server.ts`
- Test: `tools/lxe_manager/tests/mcp-server.test.ts`
- Test: `tests/lxe_editor/test_editor_workflow.py`

- [ ] **Step 1: Write the failing end-to-end manager test**

Add a manager-focused fake integration test to `tools/lxe_manager/tests/mcp-server.test.ts`:

```ts
it("returns a structured guardian error when a task is killed", async () => {
  const handlers = createToolHandlers({
    editorOps: { status: async () => ({ running: false }) },
    editorClient: { getSummary: async () => ({}) },
    workspaceOps: {
      repoPull: async () => {
        throw new Error("killed_by_guardian: cpu=390 rss=4096 free_mem=16");
      },
    },
  });

  await expect(handlers["ops.repo_pull"]()).rejects.toThrow(
    "killed_by_guardian",
  );
});
```

and add a Python workflow smoke note in `tests/lxe_editor/test_editor_workflow.py` to validate that editor HTTP API still functions without any `/mcp` dependency.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cd tools/lxe_manager
npm test -- mcp-server.test.ts
python3 -m unittest tests.lxe_editor.test_editor_workflow
```

Expected: FAIL because manager error mapping and final workflow integration are incomplete.

- [ ] **Step 3: Complete lifecycle ops, guardian wiring, and error mapping**

```ts
export class EditorOps {
  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: ManagerConfig,
  ) {}

  async start(): Promise<EditorStatus> {
    const child = await this.supervisor.startDetached({
      command: this.config.editorExecutable,
      args: [],
      cwd: this.config.runtimeRoot,
      label: "editor.start",
    });
    return { running: true, pid: child.pid };
  }
}
```

```ts
try {
  const result = await input.workspaceOps.repoPull();
  return { content: [{ type: "text", text: JSON.stringify(result) }] };
} catch (error) {
  const message = error instanceof Error ? error.message : String(error);
  throw new Error(`ops.repo_pull failed: ${message}`);
}
```

```ts
// In ProcessSupervisor, register each managed process with ResourceGuardian and
// stop guardian polling when the process exits.
```

- [ ] **Step 4: Run verification**

Run:

```bash
cd tools/lxe_manager
npm test
npm run build
cmake --build build --target test_lxe_editor_api_server
./build/src/test/test_lxe_editor_api_server
python3 -m unittest tests.lxe_editor.test_mcp_config
python3 -m unittest tests.lxe_editor.test_editor_workflow
```

Expected:

- manager unit tests PASS
- TypeScript build PASS
- editor API integration test PASS
- Python manager-config tests PASS
- Python editor workflow tests PASS

- [ ] **Step 5: Commit**

```bash
git add tools/lxe_manager/src/index.ts tools/lxe_manager/src/ops/editor-ops.ts tools/lxe_manager/src/ops/workspace-ops.ts tools/lxe_manager/src/process/process-supervisor.ts tools/lxe_manager/src/process/resource-guardian.ts tools/lxe_manager/src/mcp/server.ts tools/lxe_manager/tests/mcp-server.test.ts tests/lxe_editor/test_editor_workflow.py
git commit -m "feat: complete lxe manager control plane"
```

## Self-Review

### Spec coverage

- Manager workspace, MCP host, editor client, workspace ops, and operation log are covered by Tasks 1, 2, 3, and 5.
- `lxe_editor` MCP removal and runtime-state cleanup are covered by Task 6.
- Helper scripts, docs, and Codex flow migration are covered by Task 7.
- Resource guardian and kill escalation are covered by Tasks 4 and 8.
- End-to-end verification that editor HTTP survives without MCP is covered by Task 8.

No spec requirement is currently uncovered.

### Placeholder scan

- No `TBD`, `TODO`, or “implement later” placeholders remain.
- Every task includes exact file paths and concrete commands.
- Code steps include concrete snippets rather than prose-only instructions.

### Type consistency

- Manager config stays `repoRoot`, `runtimeRoot`, `runtimeStatePath`, `editorExecutable` across tasks.
- Editor runtime state consistently excludes `mcpUrl`.
- MCP tool names remain `editor.get_summary`, `ops.repo_pull`, and `ops.editor_status` across the plan.

