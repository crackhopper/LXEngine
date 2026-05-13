import type { ManagerConfig } from "../config.js";
import type { ProcessSupervisor } from "../process/process-supervisor.js";

export interface EditorStatus {
  running: boolean;
  pid?: number;
}

export interface EditorLogs {
  stdout: string;
  stderr: string;
  message: string;
}

export interface EditorOpsOptions {
  stopGracePeriodMs?: number;
  forceKillGracePeriodMs?: number;
  stopPollIntervalMs?: number;
}

export class EditorOps {
  private editorPid: number | undefined;
  private readonly stopGracePeriodMs: number;
  private readonly forceKillGracePeriodMs: number;
  private readonly stopPollIntervalMs: number;

  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: ManagerConfig,
    options: EditorOpsOptions = {},
  ) {
    this.stopGracePeriodMs = options.stopGracePeriodMs ?? 1500;
    this.forceKillGracePeriodMs = options.forceKillGracePeriodMs ?? 500;
    this.stopPollIntervalMs = options.stopPollIntervalMs ?? 25;
  }

  async start(): Promise<EditorStatus> {
    this.editorPid = undefined;
    const child = await this.supervisor.startDetached({
      command: this.config.editorExecutable,
      args: [],
      cwd: this.config.runtimeRoot,
      label: "editor.start",
    });
    this.editorPid = child.pid;
    return { running: true, pid: child.pid };
  }

  async stop(): Promise<EditorStatus> {
    if (!this.editorPid) {
      return { running: false };
    }

    const pid = this.editorPid;
    if (!this.supervisor.isProcessRunning(pid)) {
      this.editorPid = undefined;
      return { running: false, pid };
    }

    await this.supervisor.stopProcessTree(pid);
    let stopped = await this.supervisor.waitForProcessExit(pid, {
      timeoutMs: this.stopGracePeriodMs,
      pollIntervalMs: this.stopPollIntervalMs,
    });

    if (!stopped && this.supervisor.isProcessRunning(pid)) {
      await this.supervisor.forceKillProcessTree(pid);
      stopped = await this.supervisor.waitForProcessExit(pid, {
        timeoutMs: this.forceKillGracePeriodMs,
        pollIntervalMs: this.stopPollIntervalMs,
      });
    }

    const running = !stopped && this.supervisor.isProcessRunning(pid);
    if (!running) {
      this.editorPid = undefined;
    }
    return { running, pid };
  }

  async status(): Promise<EditorStatus> {
    if (!this.editorPid) {
      return { running: false };
    }
    return {
      running: this.supervisor.isProcessRunning(this.editorPid),
      pid: this.editorPid,
    };
  }

  async logs(): Promise<EditorLogs> {
    return {
      stdout: "",
      stderr: "",
      message: "editor logs are not captured for detached lxe_editor processes yet",
    };
  }
}
