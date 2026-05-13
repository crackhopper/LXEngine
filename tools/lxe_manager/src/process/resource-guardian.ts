import type { ResourceSample } from "./resource-sampler.js";

export interface ResourceThresholds {
  minSystemFreeMemoryBytes: number;
  maxProcessRssBytes: number;
  maxProcessCpuPercent: number;
  maxSystemCpuBusyPercent: number;
  maxSystemIoBusyPercent: number;
}

export interface ResourceGuardianInput {
  sample: () => Promise<ResourceSample>;
  kill: () => Promise<void>;
  maxConsecutiveBreaches: number;
  thresholds: ResourceThresholds;
}

export class ResourceGuardian {
  private breachCount = 0;

  constructor(private readonly input: ResourceGuardianInput) {}

  async tick(): Promise<void> {
    const sample = await this.input.sample();
    const memoryDanger =
      sample.processRssBytes >= this.input.thresholds.maxProcessRssBytes &&
      sample.systemFreeMemoryBytes <=
        this.input.thresholds.minSystemFreeMemoryBytes;
    const cpuDanger =
      sample.processCpuPercent >= this.input.thresholds.maxProcessCpuPercent &&
      sample.systemCpuBusyPercent >=
        this.input.thresholds.maxSystemCpuBusyPercent;
    const ioDanger =
      sample.systemIoBusyPercent >= this.input.thresholds.maxSystemIoBusyPercent;

    if (memoryDanger || cpuDanger || ioDanger) {
      this.breachCount += 1;
      if (this.breachCount >= this.input.maxConsecutiveBreaches) {
        await this.input.kill();
      }
      return;
    }

    this.breachCount = 0;
  }
}
