import type { ResourceSample } from "./resource-sampler.js";

export interface ResourceThresholds {
  minSystemFreeMemoryBytes: number;
  maxProcessRssBytes: number;
  maxProcessCpuPercent: number;
  maxSystemCpuBusyPercent: number;
  maxProcessIoBytesPerSecond: number;
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
  private killStarted = false;

  constructor(private readonly input: ResourceGuardianInput) {}

  async tick(): Promise<void> {
    if (this.killStarted) {
      return;
    }

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
      sample.processReadBytesPerSecond + sample.processWriteBytesPerSecond >=
        this.input.thresholds.maxProcessIoBytesPerSecond &&
      sample.systemIoBusyPercent >= this.input.thresholds.maxSystemIoBusyPercent;

    if (memoryDanger || cpuDanger || ioDanger) {
      this.breachCount += 1;
      if (this.breachCount >= this.input.maxConsecutiveBreaches) {
        this.killStarted = true;
        await this.input.kill();
      }
      return;
    }

    this.breachCount = 0;
  }
}
