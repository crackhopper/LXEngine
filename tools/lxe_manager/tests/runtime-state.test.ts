import { mkdirSync, writeFileSync } from "node:fs";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { defaultRepoRoot, resolveManagerConfig } from "../src/config.js";

describe("resolveManagerConfig", () => {
  it("builds default repo-local paths for the workspace", () => {
    const config = resolveManagerConfig({
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
    });

    expect(config.repoRoot).toBe("/repo");
    expect(config.runtimeStatePath).toBe(
      path.join("/runtime", "data", "lxe_editor", "runtime_state.yaml"),
    );
    expect(config.editorExecutable).toBe(
      path.join(
        "/repo",
        "build",
        "src",
        "demos",
        "lxe_editor",
        process.platform === "win32" ? "lxe_editor.exe" : "lxe_editor",
      ),
    );
  });

  it("allows an explicit editor executable override", () => {
    const config = resolveManagerConfig({
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
      editorExecutable: "/custom/lxe_editor",
    });

    expect(config.editorExecutable).toBe("/custom/lxe_editor");
  });

  it("resolves the default repo root from the manager source location", () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-repo-"));
    mkdirSync(path.join(repoRoot, "tools", "lxe_manager", "src"), {
      recursive: true,
    });
    writeFileSync(path.join(repoRoot, "AGENTS.md"), "");
    writeFileSync(path.join(repoRoot, "CMakeLists.txt"), "");
    const sourceUrl = new URL(
      `file://${path.join(repoRoot, "tools", "lxe_manager", "src", "index.ts")}`,
    );

    expect(defaultRepoRoot(sourceUrl.href)).toBe(repoRoot);
  });

  it("resolves the default repo root from a compiled dist location", () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-repo-"));
    mkdirSync(path.join(repoRoot, "tools", "lxe_manager", "dist", "src"), {
      recursive: true,
    });
    writeFileSync(path.join(repoRoot, "AGENTS.md"), "");
    writeFileSync(path.join(repoRoot, "CMakeLists.txt"), "");
    const distUrl = new URL(
      `file://${path.join(
        repoRoot,
        "tools",
        "lxe_manager",
        "dist",
        "src",
        "index.js",
      )}`,
    );

    expect(defaultRepoRoot(distUrl.href)).toBe(repoRoot);
  });
});
