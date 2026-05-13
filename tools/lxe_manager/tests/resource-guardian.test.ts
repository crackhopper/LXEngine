import { describe, expect, it, vi } from "vitest";
import type { ResourceSample } from "../src/process/resource-sampler.js";
import { ResourceGuardian } from "../src/process/resource-guardian.js";
import { KillPolicy } from "../src/process/kill-policy.js";

describe("resource guardian", () => {
  it("does not trip on a short spike", async () => {
    const kill = vi.fn(async () => undefined);
    const samples: ResourceSample[] = [
      {
        processRssBytes: 4096,
        processCpuPercent: 390,
        systemFreeMemoryBytes: 16,
        systemCpuBusyPercent: 99,
        processReadBytesPerSecond: 1024,
        processWriteBytesPerSecond: 1024,
        systemIoBusyPercent: 98,
      },
      {
        processRssBytes: 128,
        processCpuPercent: 15,
        systemFreeMemoryBytes: 1024,
        systemCpuBusyPercent: 40,
        processReadBytesPerSecond: 1,
        processWriteBytesPerSecond: 1,
        systemIoBusyPercent: 20,
      },
    ];
    const guardian = new ResourceGuardian({
      sample: async () => samples.shift()!,
      kill,
      maxConsecutiveBreaches: 3,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxProcessIoBytesPerSecond: 1000,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    await guardian.tick();
    expect(kill).not.toHaveBeenCalled();
  });

  it("trips after sustained pressure", async () => {
    const kill = vi.fn(async () => undefined);
    const guardian = new ResourceGuardian({
      sample: async () => ({
        processRssBytes: 4096,
        processCpuPercent: 390,
        systemFreeMemoryBytes: 16,
        systemCpuBusyPercent: 99,
        processReadBytesPerSecond: 1024,
        processWriteBytesPerSecond: 1024,
        systemIoBusyPercent: 98,
      }),
      kill,
      maxConsecutiveBreaches: 2,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxProcessIoBytesPerSecond: 1000,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    expect(kill).not.toHaveBeenCalled();
    await guardian.tick();
    expect(kill).toHaveBeenCalledOnce();
  });

  it("only kills once after sustained pressure trips", async () => {
    const kill = vi.fn(async () => undefined);
    const guardian = new ResourceGuardian({
      sample: async () => ({
        processRssBytes: 4096,
        processCpuPercent: 390,
        systemFreeMemoryBytes: 16,
        systemCpuBusyPercent: 99,
        processReadBytesPerSecond: 1024,
        processWriteBytesPerSecond: 1024,
        systemIoBusyPercent: 98,
      }),
      kill,
      maxConsecutiveBreaches: 2,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxProcessIoBytesPerSecond: 1000,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    await guardian.tick();
    await guardian.tick();

    expect(kill).toHaveBeenCalledOnce();
  });

  it("does not kill for system I/O pressure from another process", async () => {
    const kill = vi.fn(async () => undefined);
    const guardian = new ResourceGuardian({
      sample: async () => ({
        processRssBytes: 128,
        processCpuPercent: 15,
        systemFreeMemoryBytes: 1024,
        systemCpuBusyPercent: 40,
        processReadBytesPerSecond: 1,
        processWriteBytesPerSecond: 1,
        systemIoBusyPercent: 98,
      }),
      kill,
      maxConsecutiveBreaches: 1,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxProcessIoBytesPerSecond: 1000,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();

    expect(kill).not.toHaveBeenCalled();
  });

  it("resets breach count after recovery", async () => {
    const kill = vi.fn(async () => undefined);
    const dangerous: ResourceSample = {
      processRssBytes: 4096,
      processCpuPercent: 390,
      systemFreeMemoryBytes: 16,
      systemCpuBusyPercent: 99,
      processReadBytesPerSecond: 1024,
      processWriteBytesPerSecond: 1024,
      systemIoBusyPercent: 98,
    };
    const healthy: ResourceSample = {
      processRssBytes: 128,
      processCpuPercent: 15,
      systemFreeMemoryBytes: 1024,
      systemCpuBusyPercent: 40,
      processReadBytesPerSecond: 1,
      processWriteBytesPerSecond: 1,
      systemIoBusyPercent: 20,
    };
    const samples = [dangerous, healthy, dangerous];
    const guardian = new ResourceGuardian({
      sample: async () => samples.shift()!,
      kill,
      maxConsecutiveBreaches: 2,
      thresholds: {
        minSystemFreeMemoryBytes: 64,
        maxProcessRssBytes: 512,
        maxProcessCpuPercent: 350,
        maxSystemCpuBusyPercent: 95,
        maxProcessIoBytesPerSecond: 1000,
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    await guardian.tick();
    await guardian.tick();

    expect(kill).not.toHaveBeenCalled();
  });

  it("force kills only when graceful stop leaves the process running", async () => {
    const gracefulStop = vi.fn(async () => undefined);
    const forceKill = vi.fn(async () => undefined);
    const sleep = vi.fn(async () => undefined);
    const stopped = new KillPolicy({
      gracefulStop,
      forceKill,
      isRunning: () => false,
      gracePeriodMs: 10,
      sleep,
    });

    await stopped.terminate();

    expect(gracefulStop).toHaveBeenCalledOnce();
    expect(sleep).toHaveBeenCalledWith(10);
    expect(forceKill).not.toHaveBeenCalled();

    const stillRunning = new KillPolicy({
      gracefulStop,
      forceKill,
      isRunning: () => true,
      gracePeriodMs: 10,
      sleep,
    });

    await stillRunning.terminate();

    expect(forceKill).toHaveBeenCalledOnce();
  });
});
