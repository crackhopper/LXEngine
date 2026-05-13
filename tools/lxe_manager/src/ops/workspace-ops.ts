import {
  ProcessSupervisor,
  type ManagedCommand,
} from "../process/process-supervisor.js";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";

export interface BuildState {
  repoHead: string;
  repoHeadShort: string;
  repoDirty: boolean;
  buildDir: string;
  target: string;
  builtAt: string;
  exitCode: number | null;
  signal: NodeJS.Signals | null;
}

export class WorkspaceOps {
  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: {
      repoRoot: string;
      runtimeRoot: string;
      cmakeGenerator?: string;
    },
  ) {}

  buildRepoPullCommand(): ManagedCommand {
    return {
      command: "git",
      args: ["pull", "--ff-only"],
      cwd: this.config.repoRoot,
      label: "repo.pull",
    };
  }

  buildConfigureCommand(buildDir: string): ManagedCommand {
    const generatorArgs = this.config.cmakeGenerator
      ? ["-G", this.config.cmakeGenerator]
      : [];
    return {
      command: "cmake",
      args: ["-S", this.config.repoRoot, "-B", buildDir, ...generatorArgs],
      cwd: this.config.repoRoot,
      label: "build.configure",
    };
  }

  async repoPull() {
    return this.supervisor.run(this.buildRepoPullCommand());
  }

  async buildConfigure(buildDir = path.join(this.config.repoRoot, "build")) {
    return this.supervisor.run(this.buildConfigureCommand(buildDir));
  }

  async buildTarget(
    buildDir = path.join(this.config.repoRoot, "build"),
    target = "lxe_editor",
  ) {
    const result = await this.supervisor.run({
      command: "cmake",
      args: ["--build", buildDir, "--target", target],
      cwd: this.config.repoRoot,
      label: "build.target",
    });
    if (result.exitCode === 0) {
      this.writeBuildState(buildDir, target, result.exitCode, result.signal);
    }
    return result;
  }

  buildState(): BuildState | { ok: false; error: string } {
    try {
      return JSON.parse(readFileSync(this.buildStatePath(), "utf8")) as BuildState;
    } catch (error) {
      return {
        ok: false,
        error: error instanceof Error ? error.message : String(error),
      };
    }
  }

  private writeBuildState(
    buildDir: string,
    target: string,
    exitCode: number | null,
    signal: NodeJS.Signals | null,
  ): void {
    const state: BuildState = {
      repoHead: this.gitText(["rev-parse", "HEAD"]) || "unknown",
      repoHeadShort: this.gitText(["rev-parse", "--short=12", "HEAD"]) || "unknown",
      repoDirty: this.gitText(["status", "--porcelain"]).length > 0,
      buildDir,
      target,
      builtAt: new Date().toISOString(),
      exitCode,
      signal,
    };
    mkdirSync(path.dirname(this.buildStatePath()), { recursive: true });
    writeFileSync(this.buildStatePath(), `${JSON.stringify(state, null, 2)}\n`);
  }

  private buildStatePath(): string {
    return path.join(this.config.runtimeRoot, "data", "lxe_manager", "build_state.json");
  }

  private gitText(args: string[]): string {
    try {
      return execFileSync("git", args, {
        cwd: this.config.repoRoot,
        encoding: "utf8",
        stdio: ["ignore", "pipe", "ignore"],
      }).trim();
    } catch {
      return "";
    }
  }
}
