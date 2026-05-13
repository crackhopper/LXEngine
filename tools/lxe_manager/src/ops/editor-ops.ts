import type { ManagerConfig } from "../config.js";
import type { ProcessSupervisor } from "../process/process-supervisor.js";

export interface EditorStatus {
  running: boolean;
  pid?: number;
}

export class EditorOps {
  private editorPid: number | undefined;

  constructor(
    private readonly supervisor: ProcessSupervisor,
    private readonly config: ManagerConfig,
  ) {}

  async start(): Promise<EditorStatus> {
    const child = await this.supervisor.startDetached({
      command: this.config.editorExecutable,
      args: [],
      cwd: this.config.runtimeRoot,
      label: "editor.start",
    });
    this.editorPid = child.pid;
    return { running: true, pid: child.pid };
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
}
