import type { ManagerConfig } from "../config.js";
import type { ProcessSupervisor } from "../process/process-supervisor.js";
import { discoverReachableEditorClientConfig } from "../editor/runtime-state.js";

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
  startTimeoutMs?: number;
  startPollIntervalMs?: number;
}

export class EditorOps {
  private editorPid: number | undefined;
  private readonly stopGracePeriodMs: number;
  private readonly forceKillGracePeriodMs: number;
  private readonly stopPollIntervalMs: number;
  private readonly startTimeoutMs: number;
  private readonly startPollIntervalMs: number;

  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: ManagerConfig,
    options: EditorOpsOptions = {},
  ) {
    this.stopGracePeriodMs = options.stopGracePeriodMs ?? 1500;
    this.forceKillGracePeriodMs = options.forceKillGracePeriodMs ?? 500;
    this.stopPollIntervalMs = options.stopPollIntervalMs ?? 25;
    this.startTimeoutMs = options.startTimeoutMs ?? 5000;
    this.startPollIntervalMs = options.startPollIntervalMs ?? 100;
  }

  async start(): Promise<EditorStatus> {
    const current = await this.reachableStatus();
    if (current.running) {
      return current;
    }

    this.editorPid = undefined;
    const child = await this.supervisor.startDetached({
      command: this.config.editorExecutable,
      args: [],
      cwd: this.config.runtimeRoot,
      label: "editor.start",
    });
    this.editorPid = child.pid;
    return this.waitForReachableEditor(child.pid);
  }

  async stop(): Promise<EditorStatus> {
    const pid = this.editorPid ?? (await this.resolveReachableEditorPid());
    if (!pid) {
      return { running: false };
    }

    if (!this.supervisor.isProcessRunning(pid)) {
      this.editorPid = undefined;
      return { running: false, pid };
    }

    await this.supervisor.stopDetachedProcessTree(pid);
    let stopped = await this.supervisor.waitForProcessExit(pid, {
      timeoutMs: this.stopGracePeriodMs,
      pollIntervalMs: this.stopPollIntervalMs,
    });

    if (!stopped && this.supervisor.isProcessRunning(pid)) {
      await this.supervisor.forceKillDetachedProcessTree(pid);
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
    const pid = await this.resolveReachableEditorPid();
    if (!pid) {
      return { running: false };
    }
    return {
      running: this.supervisor.isProcessRunning(pid),
      pid,
    };
  }

  async logs(): Promise<EditorLogs> {
    const pid = await this.resolveReachableEditorPid();
    const logs = pid ? this.supervisor.logsForDetachedProcess(pid) : undefined;
    return {
      stdout: logs?.stdout ?? "",
      stderr: logs?.stderr ?? "",
      message: logs
        ? "captured output for the current manager-owned lxe_editor process"
        : "editor logs are only available for manager-owned lxe_editor processes",
    };
  }

  private async resolveReachableEditorPid(): Promise<number | undefined> {
    if (this.editorPid && this.supervisor.isProcessRunning(this.editorPid)) {
      return this.editorPid;
    }

    const discovered = await discoverReachableEditorClientConfig(
      this.config.runtimeStatePath,
      this.startPollIntervalMs,
    );
    const pid = discovered?.state.pid;
    if (pid && this.supervisor.isProcessRunning(pid)) {
      this.editorPid = pid;
      return pid;
    }

    this.editorPid = undefined;
    return undefined;
  }

  private async waitForReachableEditor(pid: number | undefined): Promise<EditorStatus> {
    const deadline = Date.now() + this.startTimeoutMs;
    while (Date.now() <= deadline) {
      const discovered = await discoverReachableEditorClientConfig(
        this.config.runtimeStatePath,
        this.startPollIntervalMs,
      );
      const discoveredPid = discovered?.state.pid;
      if (discoveredPid && this.supervisor.isProcessRunning(discoveredPid)) {
        this.editorPid = discoveredPid;
        return { running: true, pid: discoveredPid };
      }
      await new Promise((resolve) => setTimeout(resolve, this.startPollIntervalMs));
    }

    return { running: this.supervisor.isProcessRunning(pid), pid };
  }

  private async reachableStatus(): Promise<EditorStatus> {
    const discovered = await discoverReachableEditorClientConfig(
      this.config.runtimeStatePath,
      this.startPollIntervalMs,
    );
    const pid = discovered?.state.pid;
    if (pid && this.supervisor.isProcessRunning(pid)) {
      this.editorPid = pid;
      return { running: true, pid };
    }
    return { running: false };
  }
}
