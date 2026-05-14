export const MANAGER_RESTART_EXIT_CODE = 75;

export interface ManagerRestartResult {
  accepted: true;
  message: string;
  exitCode: typeof MANAGER_RESTART_EXIT_CODE;
  scheduleRestart: () => void;
}

export interface ManagerOpsOptions {
  exit?: (code: number) => void;
  setTimeout?: (callback: () => void, delayMs: number) => NodeJS.Timeout;
  restartDelayMs?: number;
}

export class ManagerOps {
  private readonly exit: (code: number) => void;
  private readonly setTimeout: (callback: () => void, delayMs: number) => NodeJS.Timeout;
  private readonly restartDelayMs: number;

  constructor(options: ManagerOpsOptions = {}) {
    this.exit = options.exit ?? ((code) => process.exit(code));
    this.setTimeout = options.setTimeout ?? setTimeout;
    this.restartDelayMs = options.restartDelayMs ?? 100;
  }

  async restart(): Promise<ManagerRestartResult> {
    return {
      accepted: true,
      message: "manager restart scheduled; reconnect to the MCP endpoint",
      exitCode: MANAGER_RESTART_EXIT_CODE,
      scheduleRestart: () => {
        this.setTimeout(() => {
          console.error(
            `[lxe_manager] manager restart requested; exiting with code ${MANAGER_RESTART_EXIT_CODE}`,
          );
          this.exit(MANAGER_RESTART_EXIT_CODE);
        }, this.restartDelayMs);
      },
    };
  }
}
