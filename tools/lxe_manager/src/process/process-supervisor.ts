import { spawn } from "node:child_process";

export interface ManagedCommand {
  command: string;
  args: string[];
  cwd: string;
  label: string;
}

export interface ManagedResult {
  exitCode: number;
  stdout: string;
  stderr: string;
  label: string;
}

export class ProcessSupervisor {
  async run(input: ManagedCommand): Promise<ManagedResult> {
    return new Promise((resolve, reject) => {
      const child = spawn(input.command, input.args, {
        cwd: input.cwd,
        stdio: ["ignore", "pipe", "pipe"],
      });

      let stdout = "";
      let stderr = "";

      child.stdout.on("data", (chunk: Buffer) => {
        stdout += chunk.toString();
      });
      child.stderr.on("data", (chunk: Buffer) => {
        stderr += chunk.toString();
      });
      child.on("error", reject);
      child.on("close", (code) => {
        resolve({
          exitCode: code ?? -1,
          stdout,
          stderr,
          label: input.label,
        });
      });
    });
  }
}
