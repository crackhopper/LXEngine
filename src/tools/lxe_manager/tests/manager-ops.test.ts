import { describe, expect, it, vi } from "vitest";
import { ManagerOps, MANAGER_RESTART_EXIT_CODE } from "../src/ops/manager-ops.js";

describe("manager ops", () => {
  it("accepts manager restart and supplies a post-response exit scheduler", async () => {
    const exitCodes: number[] = [];
    const setTimeout = vi.fn((callback: () => void, delayMs: number) => {
      callback();
      return {} as NodeJS.Timeout;
    });
    const managerOps = new ManagerOps({
      exit: (code) => {
        exitCodes.push(code);
      },
      setTimeout,
      restartDelayMs: 25,
    });

    const result = await managerOps.restart();

    expect(result).toMatchObject({
      accepted: true,
      message: "manager restart scheduled; reconnect to the MCP endpoint",
      exitCode: MANAGER_RESTART_EXIT_CODE,
    });
    expect(setTimeout).not.toHaveBeenCalled();
    expect(exitCodes).toEqual([]);

    result.scheduleRestart();

    expect(setTimeout).toHaveBeenCalledWith(expect.any(Function), 25);
    expect(exitCodes).toEqual([MANAGER_RESTART_EXIT_CODE]);
  });
});
