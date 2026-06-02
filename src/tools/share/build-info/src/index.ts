import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export interface BuildInfoOptions {
  binaryName: string;
  repoRoot?: string;
  projectVersion?: string;
}

export interface BuildInfoJson {
  buildInfo: string;
}

export function currentBuildInfoString(options: BuildInfoOptions): string {
  const repoRoot = options.repoRoot ?? defaultRepoRoot();
  const projectVersion =
    options.projectVersion ?? readProjectVersion(repoRoot) ?? "unknown";
  const gitCommitShort = gitText(repoRoot, ["rev-parse", "--short=12", "HEAD"]) ||
    "unknown";
  const gitDirty = gitText(repoRoot, ["status", "--porcelain"]).length > 0;
  const platform = `${process.platform}-${process.arch}`;

  return `${options.binaryName} ${projectVersion} (${gitCommitShort}${
    gitDirty ? "-dirty" : ""
  }, Node ${process.version}, ${platform})`;
}

export function currentBuildInfoJson(options: BuildInfoOptions): BuildInfoJson {
  return { buildInfo: currentBuildInfoString(options) };
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

function readProjectVersion(repoRoot: string): string | undefined {
  try {
    const text = readFileSync(path.join(repoRoot, "CMakeLists.txt"), "utf8");
    const match = text.match(/set\s*\(\s*LX_PROJECT_VERSION\s+"([^"]+)"/);
    return match?.[1];
  } catch {
    return undefined;
  }
}

function gitText(repoRoot: string, args: string[]): string {
  try {
    return execFileSync("git", args, {
      cwd: repoRoot,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return "";
  }
}
