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
