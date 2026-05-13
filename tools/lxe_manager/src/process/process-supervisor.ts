import { spawn } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { KillPolicy } from "./kill-policy.js";
import type { ResourceThresholds } from "./resource-guardian.js";
import { ResourceGuardian } from "./resource-guardian.js";
import { createProcessResourceSampler } from "./resource-sampler.js";

export interface ManagedCommand {
  command: string;
  args: string[];
  cwd: string;
  label: string;
  maxOutputBytes?: number;
}

export interface ManagedResult {
  exitCode: number | null;
  signal: NodeJS.Signals | null;
  stdout: string;
  stderr: string;
  label: string;
  stdoutTruncated: boolean;
  stderrTruncated: boolean;
  error?: string;
}

export interface ManagedProcess {
  label: string;
  pid: number | undefined;
  stop: () => Promise<void>;
  forceKill: () => Promise<void>;
  isRunning: () => boolean;
}

export interface DetachedProcess {
  label: string;
  pid: number | undefined;
}

export interface ProcessSupervisorOptions {
  guardianFactory?: (
    process: ManagedProcess,
    command: ManagedCommand,
  ) => { tick: () => Promise<void> } | undefined;
  guardianPollIntervalMs?: number;
  defaultResourceThresholds?: ResourceThresholds;
}

export class ProcessSupervisor {
  private readonly guardianPollIntervalMs: number;
  private readonly guardianFactory:
    | ((
        process: ManagedProcess,
        command: ManagedCommand,
      ) => { tick: () => Promise<void> } | undefined)
    | undefined;

  constructor(private readonly options: ProcessSupervisorOptions = {}) {
    this.guardianPollIntervalMs = options.guardianPollIntervalMs ?? 1000;
    const defaultResourceThresholds = options.defaultResourceThresholds;
    this.guardianFactory =
      options.guardianFactory ??
      (defaultResourceThresholds
        ? (managedProcess) =>
            new ResourceGuardian({
              sample: createProcessResourceSampler(managedProcess.pid),
              kill: async () => {
                const policy = new KillPolicy({
                  gracefulStop: managedProcess.stop,
                  forceKill: managedProcess.forceKill,
                  isRunning: managedProcess.isRunning,
                });
                await policy.terminate();
              },
              maxConsecutiveBreaches: 3,
              thresholds: defaultResourceThresholds,
            })
        : undefined);
  }

  async run(input: ManagedCommand): Promise<ManagedResult> {
    return new Promise((resolve) => {
      const child = spawn(input.command, input.args, {
        cwd: input.cwd,
        stdio: ["ignore", "pipe", "pipe"],
      });
      const guardian = this.startGuardian(child, input);

      const stdout = new CappedOutput(input.maxOutputBytes);
      const stderr = new CappedOutput(input.maxOutputBytes);
      let resolved = false;

      const finish = (result: ManagedResult): void => {
        if (resolved) {
          return;
        }
        resolved = true;
        guardian?.stop();
        resolve(result);
      };

      child.stdout.on("data", (chunk: Buffer) => {
        stdout.append(chunk);
      });
      child.stderr.on("data", (chunk: Buffer) => {
        stderr.append(chunk);
      });
      child.on("error", (error) => {
        finish({
          exitCode: null,
          signal: null,
          stdout: stdout.text(),
          stderr: stderr.text(),
          label: input.label,
          stdoutTruncated: stdout.truncated,
          stderrTruncated: stderr.truncated,
          error: error.message,
        });
      });
      child.on("close", (code, signal) => {
        finish({
          exitCode: code,
          signal,
          stdout: stdout.text(),
          stderr: stderr.text(),
          label: input.label,
          stdoutTruncated: stdout.truncated,
          stderrTruncated: stderr.truncated,
          error: guardian?.error(),
        });
      });
    });
  }

  async startDetached(input: ManagedCommand): Promise<DetachedProcess> {
    const child = spawn(input.command, input.args, {
      cwd: input.cwd,
      detached: true,
      stdio: "ignore",
    });
    const guardian = this.startGuardian(child, input);

    child.once("exit", () => {
      guardian?.stop();
    });
    child.once("error", () => {
      guardian?.stop();
    });
    await new Promise<void>((resolve, reject) => {
      child.once("error", reject);
      setImmediate(resolve);
    });
    child.unref();

    return { label: input.label, pid: child.pid };
  }

  isProcessRunning(pid: number | undefined): boolean {
    if (pid === undefined) {
      return false;
    }
    try {
      process.kill(pid, 0);
      return true;
    } catch {
      return false;
    }
  }

  private startGuardian(
    child: ChildProcess,
    command: ManagedCommand,
  ): { stop: () => void; error: () => string | undefined } | undefined {
    let guardianError: string | undefined;
    const guardian = this.guardianFactory?.(
      {
        label: command.label,
        pid: child.pid,
        stop: async () => {
          guardianError = `killed_by_guardian: label=${command.label}`;
          child.kill("SIGTERM");
        },
        forceKill: async () => {
          guardianError = `killed_by_guardian: label=${command.label}`;
          child.kill("SIGKILL");
        },
        isRunning: () => this.isProcessRunning(child.pid),
      },
      command,
    );
    if (!guardian) {
      return undefined;
    }

    const timer = setInterval(() => {
      void guardian.tick().catch(() => {
        guardianError = `killed_by_guardian: label=${command.label}`;
        child.kill("SIGTERM");
      });
    }, this.guardianPollIntervalMs);

    return {
      stop: () => clearInterval(timer),
      error: () => guardianError,
    };
  }
}

class CappedOutput {
  readonly maxBytes: number;
  private buffer = Buffer.alloc(0);
  truncated = false;

  constructor(maxBytes = 1024 * 1024) {
    this.maxBytes = maxBytes;
  }

  append(chunk: Buffer): void {
    if (this.maxBytes <= 0) {
      this.truncated = this.truncated || chunk.length > 0;
      return;
    }
    this.buffer = Buffer.concat([this.buffer, chunk]);
    if (this.buffer.length > this.maxBytes) {
      this.buffer = this.buffer.subarray(this.buffer.length - this.maxBytes);
      this.truncated = true;
    }
  }

  text(): string {
    return this.buffer.toString();
  }
}
