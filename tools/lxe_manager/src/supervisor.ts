import { appendFileSync, mkdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { defaultRepoRoot } from "./config.js";
import { ManagerSupervisor } from "./supervisor/manager-supervisor.js";

const managerDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const repoRoot = defaultRepoRoot(import.meta.url);
const logPath =
  process.env.LXE_MANAGER_MCP_LOG_FILE ??
  path.join(repoRoot, "data", "lxe_manager", "mcp.log");

mkdirSync(path.dirname(logPath), { recursive: true });

function log(message: string): void {
  const line = `[${new Date().toISOString()}] ${message}`;
  appendFileSync(logPath, `${line}\n`);
  console.error(line);
}

function writeStdout(chunk: Buffer): void {
  appendFileSync(logPath, chunk);
  process.stdout.write(chunk);
}

function writeStderr(chunk: Buffer): void {
  appendFileSync(logPath, chunk);
  process.stderr.write(chunk);
}

const supervisor = new ManagerSupervisor({
  managerDir,
  managerArgs: process.argv.slice(2),
  supervisorScript: fileURLToPath(import.meta.url),
  log,
  writeStdout,
  writeStderr,
});

process.on("SIGINT", () => {
  log("received SIGINT; stopping lxe_manager supervisor");
  supervisor.stop();
  process.exit(130);
});

process.on("SIGTERM", () => {
  log("received SIGTERM; stopping lxe_manager supervisor");
  supervisor.stop();
  process.exit(143);
});

supervisor.start();
