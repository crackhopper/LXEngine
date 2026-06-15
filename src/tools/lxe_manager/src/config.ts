import path from "node:path";
import { fileURLToPath } from "node:url";
import { existsSync } from "node:fs";

export interface ManagerConfig {
  repoRoot: string;
  runtimeRoot: string;
  runtimeStatePath: string;
  editorExecutable: string;
}

export function resolveManagerConfig(input: {
  repoRoot: string;
  runtimeRoot: string;
  editorExecutable?: string;
}): ManagerConfig {
  const executableName = process.platform === "win32" ? "lxe_editor.exe" : "lxe_editor";
  return {
    repoRoot: input.repoRoot,
    runtimeRoot: input.runtimeRoot,
    runtimeStatePath: path.join(
      input.runtimeRoot,
      "data",
      "lxe_editor",
      "runtime_state.yaml",
    ),
    editorExecutable:
      input.editorExecutable ??
      path.join(
        input.repoRoot,
        "build",
        "src",
        "editor",
        executableName,
      ),
  };
}

export function defaultRepoRoot(metaUrl: string = import.meta.url): string {
  let current = path.dirname(fileURLToPath(metaUrl));
  while (true) {
    if (
      existsSync(path.join(current, "AGENTS.md")) &&
      existsSync(path.join(current, "CMakeLists.txt"))
    ) {
      return current;
    }

    const parent = path.dirname(current);
    if (parent === current) {
      throw new Error(`unable to locate LXEngine repo root from ${metaUrl}`);
    }
    current = parent;
  }
}
