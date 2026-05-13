import path from "node:path";

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
        "demos",
        "lxe_editor",
        executableName,
      ),
  };
}
