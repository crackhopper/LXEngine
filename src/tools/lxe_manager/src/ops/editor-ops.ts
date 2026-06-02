import type { ManagerConfig } from "../config.js";
import type { ProcessSupervisor } from "../process/process-supervisor.js";
import { discoverReachableEditorClientConfig } from "../editor/runtime-state.js";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";

export interface EditorStatus {
  running: boolean;
  pid?: number;
}

export interface EditorLogs {
  text: string;
  message: string;
}

export interface EditorWindowConfigInput {
  key: string;
  x: number;
  y: number;
  width: number;
  height: number;
  maximized?: boolean;
  uiFontScale?: number;
}

export interface EditorWindowConfigResult {
  ok: boolean;
  path: string;
  key: string;
  window: Required<Omit<EditorWindowConfigInput, "key" | "uiFontScale">>;
  uiFontScale: number;
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

  async restart(): Promise<EditorStatus> {
    await this.stop();
    return this.start();
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
    const logPath = path.join(
      this.config.runtimeRoot,
      "data",
      "lxe_editor",
      "editor.log",
    );
    if (!existsSync(logPath)) {
      return {
        text: "",
        message: "editor log file is unavailable",
      };
    }

    return {
      text: readFileSync(logPath, "utf8"),
      message: "captured output from editor log file",
    };
  }

  async configureWindow(
    input: EditorWindowConfigInput,
  ): Promise<EditorWindowConfigResult> {
    const configPath = path.join(
      this.config.runtimeRoot,
      "data",
      "lxe_editor",
      "editor_config.yaml",
    );
    const window = {
      x: input.x,
      y: input.y,
      width: input.width,
      height: input.height,
      maximized: input.maximized ?? false,
    };
    const uiFontScale = input.uiFontScale ?? 1.0;
    const document = {
      version: 2,
      activeDisplay: input.key,
      displayDefault: {
        version: 1,
        layout: { windows: [] },
        preferences: { uiFontScale: 1.0 },
      },
      displayProfiles: [
        {
          key: input.key,
          label: input.key,
          available: false,
          overrides: {
            window,
            layout: {
              windows: defaultCompactLayout(),
            },
            preferences: { uiFontScale },
          },
        },
      ],
    };

    mkdirSync(path.dirname(configPath), { recursive: true });
    writeFileSync(configPath, `${JSON.stringify(document, null, 2)}\n`, "utf8");

    return {
      ok: true,
      path: configPath,
      key: input.key,
      window,
      uiFontScale,
      message:
        "editor_config.yaml was written; restart lxe_editor to apply native window placement",
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

function defaultCompactLayout(): Array<{
  id: string;
  visible?: boolean;
  collapsed?: boolean;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
}> {
  return [
    {
      id: "Builtin Assets",
      visible: true,
      collapsed: true,
      x: 10,
      y: 40,
      width: 320,
      height: 21,
    },
    {
      id: "Command Console",
      visible: true,
      collapsed: false,
      x: 12,
      y: 72,
      width: 390,
      height: 360,
    },
    {
      id: "Help",
      visible: true,
      collapsed: true,
      x: 900,
      y: 710,
      width: 360,
      height: 21,
    },
    {
      id: "Inspector",
      visible: true,
      collapsed: false,
      x: 900,
      y: 80,
      width: 360,
      height: 620,
    },
    {
      id: "Scene Tree",
      visible: true,
      collapsed: false,
      x: 12,
      y: 450,
      width: 420,
      height: 280,
    },
    {
      id: "Stats",
      visible: true,
      collapsed: false,
      x: 900,
      y: 20,
      width: 360,
      height: 60,
    },
    {
      id: "Toolbar",
      visible: true,
      collapsed: true,
      x: 450,
      y: 710,
      width: 420,
      height: 21,
    },
  ];
}
