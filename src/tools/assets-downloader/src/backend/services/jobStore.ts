import crypto from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import { ImportJob, ImportPreviewPlan, ImportStartRequest } from "../../shared/schema";
import { writeCacheMetadata } from "./cacheIndexer";
import { writeConvertedOutputs } from "./converter";
import { downloadToFile } from "./downloader";

export class JobStore {
  private readonly jobs = new Map<string, ImportJob>();

  start(plan: ImportPreviewPlan, request: ImportStartRequest): ImportJob {
    if (!plan.importAllowed) {
      throw new Error(`Import is not allowed for license state: ${plan.licenseStatus}`);
    }
    const job: ImportJob = {
      id: crypto.randomUUID(),
      status: "queued",
      plan,
      logs: ["queued import job"]
    };
    this.jobs.set(job.id, job);
    void this.run(job, request);
    return job;
  }

  get(id: string): ImportJob | undefined {
    return this.jobs.get(id);
  }

  private async run(job: ImportJob, request: ImportStartRequest): Promise<void> {
    try {
      job.status = "running";
      job.logs.push("creating cache directories");
      await fs.mkdir(path.join(job.plan.cachePath, "raw"), { recursive: true });

      const rawPath = path.join(job.plan.cachePath, "raw", "source.bin");
      job.logs.push(`downloading ${job.plan.url}`);
      const download = await downloadToFile(job.plan.url, rawPath);

      job.logs.push("writing converted outputs");
      await writeConvertedOutputs(job.plan, download.filePath);

      job.logs.push("writing cache metadata and license record");
      job.metadata = await writeCacheMetadata(job.plan, download.contentHash, request.userConfirmedLicense);
      job.status = "completed";
      job.logs.push("import completed");
    } catch (error) {
      job.status = "failed";
      job.error = error instanceof Error ? error.message : String(error);
      job.logs.push(`failed: ${job.error}`);
    }
  }
}
