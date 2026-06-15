import { describe, expect, it, vi } from "vitest";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { mkdtempSync } from "node:fs";
import { createServer, type Server } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { EditorOps } from "../src/ops/editor-ops.js";
import type { ManagerConfig } from "../src/config.js";
import type { ProcessSupervisor } from "../src/process/process-supervisor.js";

function config(): ManagerConfig {
  const editorExecutable = "/repo/build/src/editor/lxe_editor";
  return {
    repoRoot: "/repo",
    runtimeRoot: "/runtime",
    runtimeStatePath: "/runtime/data/lxe_editor/runtime_state.yaml",
    editorExecutable,
  };
}

function tempConfig(): { config: ManagerConfig; tokenPath: string } {
  const runtimeRoot = mkdtempSync(path.join(tmpdir(), "lxe-editor-ops-"));
  const dataDir = path.join(runtimeRoot, "data", "lxe_editor");
  mkdirSync(dataDir, { recursive: true });
  const tokenPath = path.join(dataDir, "api_token.txt");
  writeFileSync(tokenPath, "secret\n");
  return {
    tokenPath,
    config: {
      repoRoot: "/repo",
      runtimeRoot,
      runtimeStatePath: path.join(dataDir, "runtime_state.yaml"),
      editorExecutable: "/repo/build/src/editor/lxe_editor",
    },
  };
}

function listen(server: Server): Promise<number> {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      const address = server.address();
      if (!address || typeof address === "string") {
        reject(new Error("missing address"));
        return;
      }
      resolve(address.port);
    });
  });
}

function close(server: Server): Promise<void> {
  return new Promise((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });
}

function writeRuntimeState(
  config: ManagerConfig,
  tokenPath: string,
  pid: number,
  httpPort = 9,
): void {
  writeFileSync(
    config.runtimeStatePath,
    [
      `pid: ${pid}`,
      "httpHost: 127.0.0.1",
      `httpPort: ${httpPort}`,
      "wsHost: 127.0.0.1",
      "wsPort: 3001",
      `tokenFile: ${tokenPath}`,
      "startedAt: 2026-05-13T00:00:00Z",
    ].join("\n"),
  );
}

describe("editor ops", () => {
  it("waits for stop and force kills before reporting a still-running editor", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      stopDetachedProcessTree: vi.fn(async () => undefined),
      forceKillDetachedProcessTree: vi.fn(async () => undefined),
      waitForProcessExit: vi.fn(async () => false),
      isProcessRunning: vi.fn(() => true),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      stopGracePeriodMs: 5,
      forceKillGracePeriodMs: 5,
      stopPollIntervalMs: 1,
      startTimeoutMs: 1,
      startPollIntervalMs: 1,
    });

    await ops.start();
    const status = await ops.stop();

    expect(supervisor.stopDetachedProcessTree).toHaveBeenCalledWith(4321);
    expect(supervisor.waitForProcessExit).toHaveBeenNthCalledWith(1, 4321, {
      timeoutMs: 5,
      pollIntervalMs: 1,
    });
    expect(supervisor.forceKillDetachedProcessTree).toHaveBeenCalledWith(4321);
    expect(supervisor.waitForProcessExit).toHaveBeenNthCalledWith(2, 4321, {
      timeoutMs: 5,
      pollIntervalMs: 1,
    });
    expect(status).toEqual({ running: true, pid: 4321 });
  });

  it("reports stopped after graceful editor exit", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      stopDetachedProcessTree: vi.fn(async () => undefined),
      forceKillDetachedProcessTree: vi.fn(async () => undefined),
      waitForProcessExit: vi.fn(async () => true),
      isProcessRunning: vi.fn(() => false),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      stopGracePeriodMs: 5,
      forceKillGracePeriodMs: 5,
      stopPollIntervalMs: 1,
      startTimeoutMs: 1,
      startPollIntervalMs: 1,
    });

    await ops.start();
    const status = await ops.stop();

    expect(supervisor.forceKillDetachedProcessTree).not.toHaveBeenCalled();
    expect(status).toEqual({ running: false, pid: 4321 });
  });

  it("restarts the editor by stopping then starting", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      stopDetachedProcessTree: vi.fn(async () => undefined),
      forceKillDetachedProcessTree: vi.fn(async () => undefined),
      waitForProcessExit: vi.fn(async () => true),
      isProcessRunning: vi.fn(() => true),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      stopGracePeriodMs: 5,
      startTimeoutMs: 1,
      startPollIntervalMs: 1,
    });

    await ops.start();
    const status = await ops.restart();

    expect(supervisor.stopDetachedProcessTree).toHaveBeenCalledWith(4321);
    expect(supervisor.startDetached).toHaveBeenCalledTimes(2);
    expect(status).toEqual({ running: true, pid: 4321 });
  });

  it("recovers editor status from runtime state after manager restart", async () => {
    const server = createServer((_request, response) => {
      response.writeHead(200, { "content-type": "application/json" });
      response.end('{"ok":true}');
    });
    const port = await listen(server);
    const fixture = tempConfig();
    writeRuntimeState(fixture.config, fixture.tokenPath, 8765, port);
    const supervisor = {
      isProcessRunning: vi.fn((pid: number | undefined) => pid === 8765),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config);

    try {
      await expect(ops.status()).resolves.toEqual({ running: true, pid: 8765 });
    } finally {
      await close(server);
    }
  });

  it("rejects stale runtime state when reporting editor status", async () => {
    const fixture = tempConfig();
    writeRuntimeState(fixture.config, fixture.tokenPath, 8765);
    const supervisor = {
      isProcessRunning: vi.fn((pid: number | undefined) => pid === 8765),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config, {
      startPollIntervalMs: 20,
    });

    await expect(ops.status()).resolves.toEqual({ running: false });
  });

  it("reads editor logs from the shared editor log file", async () => {
    const server = createServer((_request, response) => {
      response.writeHead(200, { "content-type": "application/json" });
      response.end('{"ok":true}');
    });
    const port = await listen(server);
    const fixture = tempConfig();
    writeRuntimeState(fixture.config, fixture.tokenPath, 8765, port);
    const logPath = path.join(fixture.config.runtimeRoot, "data", "lxe_editor", "editor.log");
    writeFileSync(logPath, "out\nerr\n");
    const supervisor = {
      isProcessRunning: vi.fn((pid: number | undefined) => pid === 8765),
      logsForDetachedProcess: vi.fn(() => undefined),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config);

    try {
      await expect(ops.logs()).resolves.toEqual({
        text: "out\nerr\n",
        message: "captured output from editor log file",
      });
      expect(supervisor.logsForDetachedProcess).not.toHaveBeenCalled();
    } finally {
      await close(server);
    }
  });

  it("writes stopped-editor window configuration to editor_config.yaml", async () => {
    const fixture = tempConfig();
    const supervisor = {
      isProcessRunning: vi.fn(() => false),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config);

    const result = await ops.configureWindow({
      key: "sdl:0:Display:2560x1368:1.50",
      x: 80,
      y: 60,
      width: 1280,
      height: 820,
      maximized: false,
      uiFontScale: 1,
    });

    const document = JSON.parse(readFileSync(result.path, "utf8"));
    expect(document.version).toBe(2);
    expect(document.activeDisplay).toBe("sdl:0:Display:2560x1368:1.50");
    expect(document.displayProfiles[0].overrides.window).toEqual({
      x: 80,
      y: 60,
      width: 1280,
      height: 820,
      maximized: false,
    });
    expect(document.displayProfiles[0].overrides.layout.windows.length).toBeGreaterThan(0);
    expect(document.displayProfiles[0].overrides.preferences.uiFontScale).toBe(1);
  });

  it("does not fall back to captured detached logs when editor log file is missing", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      isProcessRunning: vi.fn(() => false),
      logsForDetachedProcess: vi.fn(() => ({
        stdout: "last out",
        stderr: "last err",
        stdoutTruncated: false,
        stderrTruncated: false,
      })),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      startTimeoutMs: 1,
      startPollIntervalMs: 1,
    });

    await ops.start();
    await expect(ops.status()).resolves.toEqual({ running: false });
    await expect(ops.logs()).resolves.toEqual({
      text: "",
      message: "editor log file is unavailable",
    });
    expect(supervisor.logsForDetachedProcess).not.toHaveBeenCalled();
  });
});
