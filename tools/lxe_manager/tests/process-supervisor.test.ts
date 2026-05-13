import { describe, expect, it } from "vitest";
import { ProcessSupervisor } from "../src/process/process-supervisor.js";
import { WorkspaceOps } from "../src/ops/workspace-ops.js";

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
    const supervisor = new ProcessSupervisor();

    await expect(
      supervisor.startDetached({
        command: "definitely-not-a-real-lxe-manager-command",
        args: [],
        cwd: process.cwd(),
        label: "missing-detached-command",
      }),
    ).rejects.toThrow("ENOENT");
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
