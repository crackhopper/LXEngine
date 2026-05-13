import {
  ProcessSupervisor,
  type ManagedCommand,
} from "../process/process-supervisor.js";
import path from "node:path";

export class WorkspaceOps {
  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: { repoRoot: string; cmakeGenerator?: string },
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
    return this.supervisor.run({
      command: "cmake",
      args: ["--build", buildDir, "--target", target],
      cwd: this.config.repoRoot,
      label: "build.target",
    });
  }
}
