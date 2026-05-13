import { describe, expect, it, vi } from "vitest";
import { mkdirSync, writeFileSync } from "node:fs";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { EditorOps } from "../src/ops/editor-ops.js";
import type { ManagerConfig } from "../src/config.js";
import type { ProcessSupervisor } from "../src/process/process-supervisor.js";

function config(): ManagerConfig {
  return {
    repoRoot: "/repo",
    runtimeRoot: "/runtime",
    runtimeStatePath: "/runtime/data/lxe_editor/runtime_state.yaml",
    editorExecutable: "/repo/build/src/demos/lxe_editor/lxe_editor",
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
      editorExecutable: "/repo/build/src/demos/lxe_editor/lxe_editor",
    },
  };
}

function writeRuntimeState(config: ManagerConfig, tokenPath: string, pid: number): void {
  writeFileSync(
    config.runtimeStatePath,
    [
      `pid: ${pid}`,
      "httpHost: 127.0.0.1",
      "httpPort: 9",
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
      stopProcessTree: vi.fn(async () => undefined),
      forceKillProcessTree: vi.fn(async () => undefined),
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

    expect(supervisor.stopProcessTree).toHaveBeenCalledWith(4321);
    expect(supervisor.waitForProcessExit).toHaveBeenNthCalledWith(1, 4321, {
      timeoutMs: 5,
      pollIntervalMs: 1,
    });
    expect(supervisor.forceKillProcessTree).toHaveBeenCalledWith(4321);
    expect(supervisor.waitForProcessExit).toHaveBeenNthCalledWith(2, 4321, {
      timeoutMs: 5,
      pollIntervalMs: 1,
    });
    expect(status).toEqual({ running: true, pid: 4321 });
  });

  it("reports stopped after graceful editor exit", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      stopProcessTree: vi.fn(async () => undefined),
      forceKillProcessTree: vi.fn(async () => undefined),
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

    expect(supervisor.forceKillProcessTree).not.toHaveBeenCalled();
    expect(status).toEqual({ running: false, pid: 4321 });
  });

  it("recovers editor status from runtime state after manager restart", async () => {
    const fixture = tempConfig();
    writeRuntimeState(fixture.config, fixture.tokenPath, 8765);
    const supervisor = {
      isProcessRunning: vi.fn((pid: number | undefined) => pid === 8765),
      logsForDetachedProcess: vi.fn(),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config);

    await expect(ops.status()).resolves.toEqual({ running: true, pid: 8765 });
  });

  it("uses captured detached editor logs when available", async () => {
    const fixture = tempConfig();
    writeRuntimeState(fixture.config, fixture.tokenPath, 8765);
    const supervisor = {
      isProcessRunning: vi.fn((pid: number | undefined) => pid === 8765),
      logsForDetachedProcess: vi.fn(() => ({
        stdout: "out",
        stderr: "err",
        stdoutTruncated: false,
        stderrTruncated: false,
      })),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, fixture.config);

    await expect(ops.logs()).resolves.toEqual({
      stdout: "out",
      stderr: "err",
      message: "captured output for the current manager-owned lxe_editor process",
    });
  });
});
