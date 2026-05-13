import { EventEmitter } from "node:events";
import type { ChildProcess, SpawnOptions } from "node:child_process";
import { describe, expect, it, vi } from "vitest";
import { ProcessSupervisor } from "../src/process/process-supervisor.js";
import { WorkspaceOps } from "../src/ops/workspace-ops.js";

function fakeChild(pid: number | undefined): ChildProcess {
  const child = new EventEmitter() as ChildProcess;
  Object.defineProperty(child, "pid", { value: pid });
  child.stdout = new EventEmitter() as ChildProcess["stdout"];
  child.stderr = new EventEmitter() as ChildProcess["stderr"];
  child.unref = vi.fn();
  return child;
}

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
    expect(result.signal).toBeNull();
    expect(result.stdout).toContain("hello from task");
  });

  it("caps captured output and marks truncation", async () => {
    const supervisor = new ProcessSupervisor();
    const result = await supervisor.run({
      command: process.execPath,
      args: ["-e", "process.stdout.write('1234567890')"],
      cwd: process.cwd(),
      label: "capped-task",
      maxOutputBytes: 4,
    });

    expect(result.exitCode).toBe(0);
    expect(result.stdout).toBe("7890");
    expect(result.stdoutTruncated).toBe(true);
  });

  it("returns a managed failure result when spawn fails", async () => {
    const supervisor = new ProcessSupervisor();
    const result = await supervisor.run({
      command: "definitely-not-a-real-lxe-manager-command",
      args: [],
      cwd: process.cwd(),
      label: "missing-command",
    });

    expect(result.exitCode).toBeNull();
    expect(result.error).toContain("ENOENT");
  });

  it("marks results killed by a guardian", async () => {
    const supervisor = new ProcessSupervisor({
      guardianPollIntervalMs: 1,
      guardianFactory: (managedProcess) => ({
        tick: async () => {
          await managedProcess.stop();
        },
      }),
    });
    const result = await supervisor.run({
      command: process.execPath,
      args: ["-e", "setTimeout(() => {}, 1000)"],
      cwd: process.cwd(),
      label: "guardian-task",
    });

    expect(result.error).toBe("killed_by_guardian: label=guardian-task");
  });

  it("rejects detached start when spawn fails", async () => {
    const supervisor = new ProcessSupervisor({
      spawnProcess: () => {
        const child = fakeChild(undefined);
        setImmediate(() => child.emit("error", new Error("spawn failed")));
        return child;
      },
    });

    await expect(
      supervisor.startDetached({
        command: "missing-command",
        args: [],
        cwd: process.cwd(),
        label: "missing-detached-command",
      }),
    ).rejects.toThrow("spawn failed");
  });

  it("targets the managed process group when guardian stops a POSIX task", async () => {
    const child = fakeChild(123);
    const killed: Array<{ pid: number; signal: NodeJS.Signals }> = [];
    const supervisor = new ProcessSupervisor({
      platform: "linux",
      guardianPollIntervalMs: 1,
      processKiller: async (pid, signal) => {
        killed.push({ pid, signal });
        child.emit("close", null, signal);
      },
      spawnProcess: (_command, _args, options?: SpawnOptions) => {
        expect(options?.detached).toBe(true);
        return child;
      },
      guardianFactory: (managedProcess) => ({
        tick: async () => {
          await managedProcess.stop();
        },
      }),
    });

    const result = await supervisor.run({
      command: "node",
      args: ["-e", "setTimeout(() => {}, 1000)"],
      cwd: process.cwd(),
      label: "guardian-group-task",
    });

    expect(result.error).toBe("killed_by_guardian: label=guardian-group-task");
    expect(killed).toContainEqual({ pid: -123, signal: "SIGTERM" });
  });

  it("can target a detached POSIX process group after external PID recovery", async () => {
    const killed: Array<{ pid: number; signal: NodeJS.Signals }> = [];
    const supervisor = new ProcessSupervisor({
      platform: "linux",
      processKiller: async (pid, signal) => {
        killed.push({ pid, signal });
      },
    });

    await supervisor.stopDetachedProcessTree(456);
    await supervisor.forceKillDetachedProcessTree(456);

    expect(killed).toEqual([
      { pid: -456, signal: "SIGTERM" },
      { pid: -456, signal: "SIGKILL" },
    ]);
  });

  it("builds a repo pull command in the repo root", async () => {
    const ops = new WorkspaceOps(new ProcessSupervisor(), { repoRoot: "/repo" });
    const command = ops.buildRepoPullCommand();

    expect(command.command).toBe("git");
    expect(command.args).toEqual(["pull", "--ff-only"]);
    expect(command.cwd).toBe("/repo");
  });

  it("only adds a CMake generator when one is configured", () => {
    const defaultOps = new WorkspaceOps(new ProcessSupervisor(), {
      repoRoot: "/repo",
    });
    expect(defaultOps.buildConfigureCommand("/build").args).toEqual([
      "-S",
      "/repo",
      "-B",
      "/build",
    ]);

    const ninjaOps = new WorkspaceOps(new ProcessSupervisor(), {
      repoRoot: "/repo",
      cmakeGenerator: "Ninja",
    });
    expect(ninjaOps.buildConfigureCommand("/build").args).toEqual([
      "-S",
      "/repo",
      "-B",
      "/build",
      "-G",
      "Ninja",
    ]);
  });
});
