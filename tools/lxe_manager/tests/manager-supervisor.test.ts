import { EventEmitter } from "node:events";
import type { ChildProcess, SpawnOptions } from "node:child_process";
import { describe, expect, it, vi } from "vitest";
import { ManagerSupervisor } from "../src/supervisor/manager-supervisor.js";
import { MANAGER_RESTART_EXIT_CODE } from "../src/ops/manager-ops.js";

function fakeChild(pid: number): ChildProcess {
  const child = new EventEmitter() as ChildProcess;
  Object.defineProperty(child, "pid", { value: pid });
  Object.assign(child, {
    unref: vi.fn(),
    stdout: new EventEmitter() as ChildProcess["stdout"],
    stderr: new EventEmitter() as ChildProcess["stderr"],
  });
  return child as ChildProcess;
}

describe("manager supervisor", () => {
  it("starts the MCP server child with the forwarded manager arguments", () => {
    const child = fakeChild(101);
    const spawnProcess = vi.fn(() => child);
    const supervisor = new ManagerSupervisor({
      managerDir: "/repo/tools/lxe_manager",
      managerArgs: ["--host", "0.0.0.0", "--port", "3880"],
      supervisorScript: "/repo/tools/lxe_manager/src/supervisor.ts",
      spawnProcess,
      exit: vi.fn(),
      log: vi.fn(),
    });

    supervisor.start();

    expect(spawnProcess).toHaveBeenCalledWith(
      process.execPath,
      [
        "--import",
        "tsx",
        "./src/index.ts",
        "--host",
        "0.0.0.0",
        "--port",
        "3880",
      ],
      expect.objectContaining({
        cwd: "/repo/tools/lxe_manager",
        stdio: ["ignore", "pipe", "pipe"],
      }) as SpawnOptions,
    );
  });

  it("starts a replacement supervisor when the MCP server requests restart", () => {
    const child = fakeChild(102);
    const replacement = fakeChild(202);
    const spawnProcess = vi
      .fn()
      .mockReturnValueOnce(child)
      .mockReturnValueOnce(replacement);
    const exit = vi.fn();
    const supervisor = new ManagerSupervisor({
      managerDir: "/repo/tools/lxe_manager",
      managerArgs: ["--host", "0.0.0.0"],
      supervisorScript: "/repo/tools/lxe_manager/src/supervisor.ts",
      nodeExecArgv: ["--import", "tsx"],
      spawnProcess,
      exit,
      log: vi.fn(),
    });

    supervisor.start();
    child.emit("exit", MANAGER_RESTART_EXIT_CODE, null);

    expect(spawnProcess).toHaveBeenNthCalledWith(
      2,
      process.execPath,
      [
        "--import",
        "tsx",
        "/repo/tools/lxe_manager/src/supervisor.ts",
        "--host",
        "0.0.0.0",
      ],
      expect.objectContaining({
        cwd: "/repo/tools/lxe_manager",
        detached: true,
        stdio: "ignore",
      }) as SpawnOptions,
    );
    expect(exit).toHaveBeenCalledWith(0);
  });

  it("exits without replacement when the MCP server exits with another code", () => {
    const child = fakeChild(103);
    const spawnProcess = vi.fn(() => child);
    const exit = vi.fn();
    const supervisor = new ManagerSupervisor({
      managerDir: "/repo/tools/lxe_manager",
      managerArgs: [],
      supervisorScript: "/repo/tools/lxe_manager/src/supervisor.ts",
      spawnProcess,
      exit,
      log: vi.fn(),
    });

    supervisor.start();
    child.emit("exit", 2, null);

    expect(spawnProcess).toHaveBeenCalledTimes(1);
    expect(exit).toHaveBeenCalledWith(2);
  });
});
