import { spawn } from "node:child_process";

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

export class ProcessSupervisor {
  async run(input: ManagedCommand): Promise<ManagedResult> {
    return new Promise((resolve) => {
      const child = spawn(input.command, input.args, {
        cwd: input.cwd,
        stdio: ["ignore", "pipe", "pipe"],
      });

      const stdout = new CappedOutput(input.maxOutputBytes);
      const stderr = new CappedOutput(input.maxOutputBytes);

      child.stdout.on("data", (chunk: Buffer) => {
        stdout.append(chunk);
      });
      child.stderr.on("data", (chunk: Buffer) => {
        stderr.append(chunk);
      });
      child.on("error", (error) => {
        resolve({
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
        resolve({
          exitCode: code,
          signal,
          stdout: stdout.text(),
          stderr: stderr.text(),
          label: input.label,
          stdoutTruncated: stdout.truncated,
          stderrTruncated: stderr.truncated,
        });
      });
    });
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
