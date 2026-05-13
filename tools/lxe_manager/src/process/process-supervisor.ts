import { spawn } from "node:child_process";
import type { ChildProcess, SpawnOptions } from "node:child_process";
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

export interface DetachedProcessLogs {
  stdout: string;
  stderr: string;
  stdoutTruncated: boolean;
  stderrTruncated: boolean;
}

export interface ProcessSupervisorOptions {
  guardianFactory?: (
    process: ManagedProcess,
    command: ManagedCommand,
  ) => { tick: () => Promise<void> } | undefined;
  guardianPollIntervalMs?: number;
  defaultResourceThresholds?: ResourceThresholds;
  platform?: NodeJS.Platform;
  processKiller?: (
    pid: number,
    signal: NodeJS.Signals,
  ) => Promise<void> | void;
  sleep?: (ms: number) => Promise<void>;
  spawnProcess?: (
    command: string,
    args: string[],
    options: SpawnOptions,
  ) => ChildProcess;
}

export class ProcessSupervisor {
  private readonly guardianPollIntervalMs: number;
  private readonly detachedPids = new Set<number>();
  private readonly detachedOutputs = new Map<
    number,
    { stdout: CappedOutput; stderr: CappedOutput }
  >();
  private readonly guardianFactory:
    | ((
        process: ManagedProcess,
        command: ManagedCommand,
      ) => { tick: () => Promise<void> } | undefined)
    | undefined;
  private readonly platform: NodeJS.Platform;
  private readonly processKiller: (
    pid: number,
    signal: NodeJS.Signals,
  ) => Promise<void> | void;
  private readonly sleep: (ms: number) => Promise<void>;
  private readonly spawnProcess: (
    command: string,
    args: string[],
    options: SpawnOptions,
  ) => ChildProcess;

  constructor(options: ProcessSupervisorOptions = {}) {
    this.guardianPollIntervalMs = options.guardianPollIntervalMs ?? 1000;
    this.platform = options.platform ?? process.platform;
    this.processKiller =
      options.processKiller ??
      ((pid, signal) => defaultProcessKiller(this.platform, pid, signal));
    this.sleep = options.sleep ?? defaultSleep;
    this.spawnProcess = options.spawnProcess ?? spawn;
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
      const detached = this.platform !== "win32";
      const child = this.spawnProcess(input.command, input.args, {
        cwd: input.cwd,
        detached,
        stdio: ["ignore", "pipe", "pipe"],
      });
      if (detached) {
        this.registerDetachedPid(child.pid);
      }
      const guardian = this.startGuardian(child, input, detached);

      const stdout = new CappedOutput(input.maxOutputBytes);
      const stderr = new CappedOutput(input.maxOutputBytes);
      let resolved = false;

      const finish = (result: ManagedResult): void => {
        if (resolved) {
          return;
        }
        resolved = true;
        guardian?.stop();
        this.unregisterDetachedPid(child.pid);
        resolve(result);
      };

      child.stdout?.on("data", (chunk: Buffer) => {
        stdout.append(chunk);
      });
      child.stderr?.on("data", (chunk: Buffer) => {
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
    const child = this.spawnProcess(input.command, input.args, {
      cwd: input.cwd,
      detached: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    this.registerDetachedPid(child.pid);
    const output = this.registerDetachedOutput(child.pid, input.maxOutputBytes);
    const guardian = this.startGuardian(child, input, true);

    child.stdout?.on("data", (chunk: Buffer) => {
      output?.stdout.append(chunk);
    });
    child.stderr?.on("data", (chunk: Buffer) => {
      output?.stderr.append(chunk);
    });
    child.once("exit", () => {
      guardian?.stop();
      this.unregisterDetachedPid(child.pid);
    });
    child.once("error", () => {
      guardian?.stop();
      this.unregisterDetachedPid(child.pid);
    });
    await new Promise<void>((resolve, reject) => {
      child.once("error", reject);
      setImmediate(resolve);
    });
    child.unref();

    return { label: input.label, pid: child.pid };
  }

  logsForDetachedProcess(pid: number | undefined): DetachedProcessLogs | undefined {
    if (pid === undefined) {
      return undefined;
    }
    const output = this.detachedOutputs.get(pid);
    if (!output) {
      return undefined;
    }
    return {
      stdout: output.stdout.text(),
      stderr: output.stderr.text(),
      stdoutTruncated: output.stdout.truncated,
      stderrTruncated: output.stderr.truncated,
    };
  }

  async stopProcessTree(pid: number | undefined): Promise<void> {
    await this.killProcessTree(pid, "SIGTERM");
  }

  async forceKillProcessTree(pid: number | undefined): Promise<void> {
    await this.killProcessTree(pid, "SIGKILL");
  }

  async waitForProcessExit(
    pid: number | undefined,
    input: { timeoutMs: number; pollIntervalMs: number },
  ): Promise<boolean> {
    if (pid === undefined) {
      return true;
    }

    const deadline = Date.now() + input.timeoutMs;
    while (this.isProcessRunning(pid)) {
      if (Date.now() >= deadline) {
        return false;
      }
      await this.sleep(Math.max(1, input.pollIntervalMs));
    }

    return true;
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
    detached: boolean,
  ): { stop: () => void; error: () => string | undefined } | undefined {
    let guardianError: string | undefined;
    const guardian = this.guardianFactory?.(
      {
        label: command.label,
        pid: child.pid,
        stop: async () => {
          guardianError = `killed_by_guardian: label=${command.label}`;
          await this.killProcessTree(child.pid, "SIGTERM", detached);
        },
        forceKill: async () => {
          guardianError = `killed_by_guardian: label=${command.label}`;
          await this.killProcessTree(child.pid, "SIGKILL", detached);
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
        void this.killProcessTree(child.pid, "SIGTERM", detached);
      });
    }, this.guardianPollIntervalMs);

    return {
      stop: () => clearInterval(timer),
      error: () => guardianError,
    };
  }

  private async killProcessTree(
    pid: number | undefined,
    signal: NodeJS.Signals,
    detached = pid !== undefined && this.detachedPids.has(pid),
  ): Promise<void> {
    if (pid === undefined) {
      return;
    }

    const targetPid = this.platform !== "win32" && detached ? -pid : pid;
    await this.processKiller(targetPid, signal);
  }

  private registerDetachedPid(pid: number | undefined): void {
    if (pid !== undefined) {
      this.detachedPids.add(pid);
    }
  }

  private unregisterDetachedPid(pid: number | undefined): void {
    if (pid !== undefined) {
      this.detachedPids.delete(pid);
    }
  }

  private registerDetachedOutput(
    pid: number | undefined,
    maxOutputBytes: number | undefined,
  ): { stdout: CappedOutput; stderr: CappedOutput } | undefined {
    if (pid === undefined) {
      return undefined;
    }
    const output = {
      stdout: new CappedOutput(maxOutputBytes),
      stderr: new CappedOutput(maxOutputBytes),
    };
    this.detachedOutputs.set(pid, output);
    return output;
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

async function defaultProcessKiller(
  platform: NodeJS.Platform,
  pid: number,
  signal: NodeJS.Signals,
): Promise<void> {
  if (platform === "win32") {
    await killWindowsProcessTree(pid, signal);
    return;
  }

  try {
    process.kill(pid, signal);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ESRCH") {
      throw error;
    }
  }
}

function killWindowsProcessTree(
  pid: number,
  signal: NodeJS.Signals,
): Promise<void> {
  const args = ["/PID", String(pid), "/T"];
  if (signal === "SIGKILL") {
    args.push("/F");
  }

  return new Promise((resolve, reject) => {
    const child = spawn("taskkill", args, { stdio: "ignore" });
    child.once("error", reject);
    child.once("close", () => resolve());
  });
}

function defaultSleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
