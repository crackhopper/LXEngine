import { describe, expect, it } from "vitest";
import { resolveManagerConfig } from "../src/config.js";

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
