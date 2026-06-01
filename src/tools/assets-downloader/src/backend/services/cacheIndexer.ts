import fs from "node:fs/promises";
import path from "node:path";
import YAML from "yaml";
import { CacheAssetMetadata, ImportPreviewPlan } from "../../shared/schema";
import { actualConvertedOutputs } from "./converter";
import { resolveCacheRoot } from "./paths";

export async function writeCacheMetadata(
  plan: ImportPreviewPlan,
  contentHash: string,
  userConfirmedLicense?: { confirmationSource: string }
): Promise<CacheAssetMetadata> {
  const now = new Date().toISOString();
  const licenseFile = path.join("license", "license.yaml");
  const metadata: CacheAssetMetadata = {
    schemaVersion: 1,
    displayName: plan.assetId,
    kind: plan.kind,
    source: {
      id: plan.sourceId,
      name: plan.sourceName,
      url: plan.url
    },
    asset: {
      id: plan.assetId,
      variant: plan.variant,
      cacheUriBase: plan.cacheUriBase
    },
    license: {
      status: plan.licenseStatus,
      name: plan.licenseName,
      url: plan.licenseUrl,
      file: licenseFile,
      confirmedAt: plan.licenseStatus === "user_confirmed" ? now : undefined,
      confirmationSource: userConfirmedLicense?.confirmationSource
    },
    download: {
      url: plan.url,
      downloadedAt: now,
      contentHash
    },
    convertedOutputs: actualConvertedOutputs(plan),
    manualNotes: plan.manualNotes
  };

  await fs.mkdir(path.join(plan.cachePath, "license"), { recursive: true });
  await fs.writeFile(path.join(plan.cachePath, licenseFile), YAML.stringify(metadata.license), "utf8");
  await fs.writeFile(path.join(plan.cachePath, "source.yaml"), YAML.stringify(metadata), "utf8");
  return metadata;
}

export async function listCacheAssets(cacheRoot = resolveCacheRoot()): Promise<CacheAssetMetadata[]> {
  const assets: CacheAssetMetadata[] = [];
  if (!(await exists(cacheRoot))) {
    return assets;
  }
  const sourceDirs = await fs.readdir(cacheRoot, { withFileTypes: true });
  for (const sourceDir of sourceDirs.filter((entry) => entry.isDirectory())) {
    const sourcePath = path.join(cacheRoot, sourceDir.name);
    const assetDirs = await fs.readdir(sourcePath, { withFileTypes: true });
    for (const assetDir of assetDirs.filter((entry) => entry.isDirectory())) {
      const assetPath = path.join(sourcePath, assetDir.name);
      const variantDirs = await fs.readdir(assetPath, { withFileTypes: true });
      for (const variantDir of variantDirs.filter((entry) => entry.isDirectory())) {
        const metadataPath = path.join(assetPath, variantDir.name, "source.yaml");
        if (await exists(metadataPath)) {
          const parsed = YAML.parse(await fs.readFile(metadataPath, "utf8"));
          assets.push(parsed as CacheAssetMetadata);
        }
      }
    }
  }
  return assets;
}

async function exists(filePath: string): Promise<boolean> {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}
