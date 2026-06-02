import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { currentBuildInfoJson, currentBuildInfoString } from "./index";

function git(repoRoot: string, args: string[]): void {
  execFileSync("git", args, { cwd: repoRoot, stdio: "ignore" });
}

describe("Node build-info", () => {
  it("returns one composed buildInfo string without split fields", () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-node-build-info-"));
    writeFileSync(path.join(repoRoot, "AGENTS.md"), "");
    writeFileSync(
      path.join(repoRoot, "CMakeLists.txt"),
      'set(LX_PROJECT_VERSION "9.8.7-test" CACHE STRING "")\n',
    );
    writeFileSync(path.join(repoRoot, "file.txt"), "one\n");
    git(repoRoot, ["init"]);
    git(repoRoot, ["add", "."]);
    git(repoRoot, [
      "-c",
      "user.name=Test",
      "-c",
      "user.email=test@example.com",
      "commit",
      "-m",
      "initial",
    ]);

    const buildInfo = currentBuildInfoString({
      binaryName: "test_tool",
      repoRoot,
    });
    expect(buildInfo).toContain("test_tool 9.8.7-test");
    expect(buildInfo).toContain("Node ");
    expect(buildInfo).not.toContain("gitCommit");

    const json = currentBuildInfoJson({ binaryName: "test_tool", repoRoot });
    expect(json).toEqual({ buildInfo });
    expect("gitCommit" in json).toBe(false);
  });
});
