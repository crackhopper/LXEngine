import {
  ProcessSupervisor,
  type ManagedCommand,
} from "../process/process-supervisor.js";

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
}
