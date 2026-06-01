// @vitest-environment node

import fs from "node:fs/promises";
import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import YAML from "yaml";
import { afterEach, describe, expect, it, vi } from "vitest";
import { listCacheAssets } from "../services/cacheIndexer";
import { createImportPreviewPlan } from "../services/importPlanner";
import { JobStore } from "../services/jobStore";
import { loadCatalog } from "../services/sourceRegistry";

describe("cache import", () => {
  afterEach(() => {
    vi.unstubAllEnvs();
  });

  it("does not write converted output for unknown license imports", async () => {
    const cacheRoot = await mkdtemp(path.join(os.tmpdir(), "lxe-assets-cache-"));
    vi.stubEnv("LXENGINE_ASSET_CACHE", cacheRoot);
    const catalog = await loadCatalog();
    const plan = createImportPreviewPlan(catalog, {
      url: fixtureUrl(),
      kind: "material"
    });
    const jobs = new JobStore();

    expect(() => jobs.start(plan, { url: fixtureUrl(), kind: "material" })).toThrow(/not allowed/);
    await expect(fs.access(path.join(cacheRoot, "manual"))).rejects.toThrow();
  });

  it("imports a user-confirmed material fixture and writes metadata", async () => {
    const cacheRoot = await mkdtemp(path.join(os.tmpdir(), "lxe-assets-cache-"));
    vi.stubEnv("LXENGINE_ASSET_CACHE", cacheRoot);
    const catalog = await loadCatalog();
    const request = {
      url: fixtureUrl(),
      sourceId: "ambientcg",
      assetId: "fixture_material",
      variant: "test",
      kind: "material" as const,
      userConfirmedLicense: {
        licenseName: "Fixture License",
        licenseUrl: "https://example.com/license",
        confirmationSource: "unit test"
      }
    };
    const plan = createImportPreviewPlan(catalog, request);
    const jobs = new JobStore();
    const job = jobs.start(plan, request);
    const completed = await waitForJob(jobs, job.id);

    expect(completed.error).toBeUndefined();
    expect(completed.status).toBe("completed");
    const assets = await listCacheAssets(cacheRoot);
    expect(assets).toHaveLength(1);
    expect(assets[0].license.status).toBe("user_confirmed");
    expect(assets[0].license.confirmedAt).toBeTruthy();
    expect(assets[0].download.contentHash).toMatch(/^[a-f0-9]{64}$/);
    expect(assets[0].convertedOutputs.some((output) => output.uri.endsWith("/converted/material.yaml"))).toBe(true);

    const materialPath = path.join(cacheRoot, "ambientcg", "fixture_material", "test", "converted", "material.yaml");
    const material = YAML.parse(await fs.readFile(materialPath, "utf8"));
    expect(material.baseColor.texture).toBe("cache://ambientcg/fixture_material/test/converted/textures/basecolor.png");
    expect(material.metallicRoughness.metallicChannel).toBe("b");
    expect(material.emissive.texture).toBeNull();
  });
});

function fixtureUrl(): string {
  return new URL("fixtures/tiny.bin", import.meta.url).toString();
}

async function waitForJob(jobs: JobStore, id: string) {
  for (let attempt = 0; attempt < 50; attempt += 1) {
    const job = jobs.get(id);
    if (job && (job.status === "completed" || job.status === "failed")) {
      return job;
    }
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error("timed out waiting for job");
}
