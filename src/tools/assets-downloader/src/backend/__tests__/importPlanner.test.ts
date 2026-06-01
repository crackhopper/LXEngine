// @vitest-environment node

import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { createImportPreviewPlan } from "../services/importPlanner";
import { loadCatalog } from "../services/sourceRegistry";

describe("import planner", () => {
  afterEach(() => {
    vi.unstubAllEnvs();
  });

  it("creates a preview plan without writing files", async () => {
    const cacheRoot = await mkdtemp(path.join(os.tmpdir(), "lxe-assets-cache-"));
    vi.stubEnv("LXENGINE_ASSET_CACHE", cacheRoot);
    const catalog = await loadCatalog();

    const plan = createImportPreviewPlan(catalog, {
      url: "https://polyhaven.com/a/studio_small_03"
    });

    expect(plan.licenseStatus).toBe("verified");
    expect(plan.importAllowed).toBe(true);
    expect(plan.cachePath).toContain(cacheRoot);
    expect(plan.filesToWrite).toContain("source.yaml");
    expect(plan.cacheUriBase).toBe("cache://polyhaven/studio_small_03/2k-hdr");
  });

  it("blocks unknown imports unless the user confirms license", async () => {
    const catalog = await loadCatalog();
    const unknown = createImportPreviewPlan(catalog, {
      url: "https://example.com/unlisted.glb"
    });
    expect(unknown.licenseStatus).toBe("unknown");
    expect(unknown.importAllowed).toBe(false);

    const confirmed = createImportPreviewPlan(catalog, {
      url: "https://example.com/unlisted.glb",
      userConfirmedLicense: {
        licenseName: "Local permission",
        licenseUrl: "https://example.com/license",
        confirmationSource: "manual test"
      }
    });
    expect(confirmed.licenseStatus).toBe("user_confirmed");
    expect(confirmed.importAllowed).toBe(true);
  });

  it("plans point-cloud outputs for PLY assets", async () => {
    const catalog = await loadCatalog();

    const plan = createImportPreviewPlan(catalog, {
      url: "https://huggingface.co/datasets/Voxel51/gaussian_splatting/resolve/main/FO_dataset/train/point_cloud/iteration_7000/point_cloud.ply"
    });

    expect(plan.kind).toBe("point-cloud");
    expect(plan.sourceId).toBe("voxel51-gaussian-splatting");
    expect(plan.assetId).toBe("train_iteration_7000");
    expect(plan.variant).toBe("iteration-7000");
    expect(plan.filesToWrite).toContain("converted/point_cloud.ply");
    expect(plan.filesToWrite).toContain("converted/point_cloud.asset.yaml");
    expect(plan.cacheUriBase).toBe("cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000");
  });
});
