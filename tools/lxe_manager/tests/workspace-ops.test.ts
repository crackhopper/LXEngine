import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { describe, expect, it, vi } from "vitest";
import type { ProcessSupervisor } from "../src/process/process-supervisor.js";
import { WorkspaceOps } from "../src/ops/workspace-ops.js";

function git(repoRoot: string, args: string[]): void {
  execFileSync("git", args, { cwd: repoRoot, stdio: "ignore" });
}

describe("workspace ops", () => {
  it("writes build state after a successful target build", async () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-repo-"));
    const runtimeRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-runtime-"));
    writeFileSync(path.join(repoRoot, "file.txt"), "one\n");
    git(repoRoot, ["init"]);
    git(repoRoot, ["add", "file.txt"]);
    git(repoRoot, [
      "-c",
      "user.name=Test",
      "-c",
      "user.email=test@example.com",
      "commit",
      "-m",
      "initial",
    ]);

    const supervisor = {
      run: vi.fn(async () => ({
        exitCode: 0,
        signal: null,
        stdout: "",
        stderr: "",
        label: "build.target",
        stdoutTruncated: false,
        stderrTruncated: false,
      })),
    } as unknown as ProcessSupervisor;
    const ops = new WorkspaceOps(supervisor, { repoRoot, runtimeRoot });

    await ops.buildTarget(path.join(repoRoot, "build"), "lxe_editor");

    const statePath = path.join(
      runtimeRoot,
      "data",
      "lxe_manager",
      "build_state.json",
    );
    expect(existsSync(statePath)).toBe(true);
    const state = JSON.parse(readFileSync(statePath, "utf8")) as {
      repoHead: string;
      repoHeadShort: string;
      repoDirty: boolean;
      target: string;
      exitCode: number;
    };
    expect(state.repoHead).toHaveLength(40);
    expect(state.repoHeadShort).toHaveLength(12);
    expect(state.repoDirty).toBe(false);
    expect(state.target).toBe("lxe_editor");
    expect(state.exitCode).toBe(0);
    expect(ops.buildState()).toMatchObject({
      repoHead: state.repoHead,
      target: "lxe_editor",
    });
  });
});
