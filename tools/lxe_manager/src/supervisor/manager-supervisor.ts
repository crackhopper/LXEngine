import { spawn } from "node:child_process";
import type { ChildProcess, SpawnOptions } from "node:child_process";
import { MANAGER_RESTART_EXIT_CODE } from "../ops/manager-ops.js";

export interface ManagerSupervisorOptions {
  managerDir: string;
  managerArgs: string[];
  supervisorScript: string;
  nodeExecArgv?: string[];
  spawnProcess?: (
    command: string,
    args: string[],
    options: SpawnOptions,
  ) => ChildProcess;
  exit?: (code: number) => void;
  log?: (message: string) => void;
  writeStdout?: (chunk: Buffer) => void;
  writeStderr?: (chunk: Buffer) => void;
}

export class ManagerSupervisor {
  private readonly spawnProcess: (
    command: string,
    args: string[],
    options: SpawnOptions,
  ) => ChildProcess;
  private readonly exit: (code: number) => void;
  private readonly log: (message: string) => void;
  private readonly nodeExecArgv: string[];
  private readonly writeStdout: (chunk: Buffer) => void;
  private readonly writeStderr: (chunk: Buffer) => void;
  private activeChild: ChildProcess | undefined;

  constructor(private readonly options: ManagerSupervisorOptions) {
    this.spawnProcess = options.spawnProcess ?? spawn;
    this.exit = options.exit ?? process.exit;
    this.log = options.log ?? console.error;
    this.nodeExecArgv = options.nodeExecArgv ?? process.execArgv;
    this.writeStdout = options.writeStdout ?? ((chunk) => process.stdout.write(chunk));
    this.writeStderr = options.writeStderr ?? ((chunk) => process.stderr.write(chunk));
  }

  start(): void {
    const child = this.spawnManagerChild();
    child.on("exit", (code, signal) => {
      this.activeChild = undefined;
      this.log(
        `lxe_manager child exited code=${code ?? ""} signal=${signal ?? ""}`,
      );
      if (code === MANAGER_RESTART_EXIT_CODE) {
        this.startReplacementSupervisor();
        this.exit(0);
        return;
      }
      this.exit(code ?? 1);
    });
  }

  stop(): void {
    if (this.activeChild && !this.activeChild.killed) {
      this.activeChild.kill("SIGTERM");
    }
  }

  private spawnManagerChild(): ChildProcess {
    const args = ["--import", "tsx", "./src/index.ts", ...this.options.managerArgs];
    this.log(`starting lxe_manager: node ${args.join(" ")}`);
    const child = this.spawnProcess(process.execPath, args, {
      cwd: this.options.managerDir,
      stdio: ["ignore", "pipe", "pipe"],
    });
    this.activeChild = child;
    this.log(`lxe_manager child pid=${child.pid ?? ""}`);
    child.stdout?.on("data", (chunk: Buffer) => {
      this.writeStdout(chunk);
    });
    child.stderr?.on("data", (chunk: Buffer) => {
      this.writeStderr(chunk);
    });
    return child;
  }

  private startReplacementSupervisor(): void {
    const args = [
      ...this.nodeExecArgv,
      this.options.supervisorScript,
      ...this.options.managerArgs,
    ];
    this.log(`starting replacement lxe_manager supervisor: node ${args.join(" ")}`);
    const child = this.spawnProcess(process.execPath, args, {
      cwd: this.options.managerDir,
      stdio: "inherit",
    });
    child.unref();
  }
}
