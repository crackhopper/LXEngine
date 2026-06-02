import { readFile } from "node:fs/promises";
import os from "node:os";

export interface ResourceSample {
  processRssBytes: number;
  processCpuPercent: number;
  systemFreeMemoryBytes: number;
  systemCpuBusyPercent: number;
  processReadBytesPerSecond: number;
  processWriteBytesPerSecond: number;
  systemIoBusyPercent: number;
}

export function createProcessResourceSampler(
  pid: number | undefined,
): () => Promise<ResourceSample> {
  return async () => ({
    processRssBytes: await readProcessRssBytes(pid),
    processCpuPercent: 0,
    systemFreeMemoryBytes: os.freemem(),
    systemCpuBusyPercent: 0,
    processReadBytesPerSecond: 0,
    processWriteBytesPerSecond: 0,
    systemIoBusyPercent: 0,
  });
}

async function readProcessRssBytes(pid: number | undefined): Promise<number> {
  if (pid === undefined || process.platform !== "linux") {
    return 0;
  }
  try {
    const status = await readFile(`/proc/${pid}/status`, "utf-8");
    const match = /^VmRSS:\s+(\d+)\s+kB$/m.exec(status);
    return match ? Number(match[1]) * 1024 : 0;
  } catch {
    return 0;
  }
}
