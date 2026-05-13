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

export class EditorOps {
  private editorPid: number | undefined;

  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: ManagerConfig,
  ) {}

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

    if (this.supervisor.isProcessRunning(this.editorPid)) {
      process.kill(this.editorPid, "SIGTERM");
    }
    const pid = this.editorPid;
    this.editorPid = undefined;
    return { running: false, pid };
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
