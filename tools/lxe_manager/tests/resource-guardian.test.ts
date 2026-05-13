import { describe, expect, it, vi } from "vitest";
import type { ResourceSample } from "../src/process/resource-sampler.js";
import { ResourceGuardian } from "../src/process/resource-guardian.js";

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
        maxSystemIoBusyPercent: 95,
      },
    });

    await guardian.tick();
    expect(kill).not.toHaveBeenCalled();
    await guardian.tick();
    expect(kill).toHaveBeenCalledOnce();
  });
});
