import { describe, expect, it, vi } from "vitest";
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

describe("editor ops", () => {
  it("waits for stop and force kills before reporting a still-running editor", async () => {
    const supervisor = {
      startDetached: vi.fn(async () => ({ label: "editor.start", pid: 4321 })),
      stopProcessTree: vi.fn(async () => undefined),
      forceKillProcessTree: vi.fn(async () => undefined),
      waitForProcessExit: vi.fn(async () => false),
      isProcessRunning: vi.fn(() => true),
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      stopGracePeriodMs: 5,
      forceKillGracePeriodMs: 5,
      stopPollIntervalMs: 1,
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
    } as unknown as ProcessSupervisor;
    const ops = new EditorOps(supervisor, config(), {
      stopGracePeriodMs: 5,
      forceKillGracePeriodMs: 5,
      stopPollIntervalMs: 1,
    });

    await ops.start();
    const status = await ops.stop();

    expect(supervisor.forceKillProcessTree).not.toHaveBeenCalled();
    expect(status).toEqual({ running: false, pid: 4321 });
  });
});
