import fs from "node:fs/promises";
import path from "node:path";
import YAML from "yaml";
import { buildCacheUri } from "../../shared/cacheUri";
import { ImportPreviewPlan } from "../../shared/schema";
import { plannedConvertedOutputs } from "./importPlanner";

export async function writeConvertedOutputs(plan: ImportPreviewPlan, rawFilePath: string): Promise<void> {
  switch (plan.kind) {
    case "environment":
      await writeEnvironment(plan, rawFilePath);
      return;
    case "model":
      await writeModel(plan, rawFilePath);
      return;
    case "point-cloud":
      await writePointCloud(plan, rawFilePath);
      return;
    case "material":
      await writeMaterial(plan, rawFilePath);
      return;
    case "manual":
      await writeManualNotes(plan);
      return;
  }
}

async function writeEnvironment(plan: ImportPreviewPlan, rawFilePath: string): Promise<void> {
  const extension = path.extname(urlLikePath(plan.url)).toLowerCase();
  const outputName = extension === ".exr" ? "environment.exr" : "environment.hdr";
  const outputPath = path.join(plan.cachePath, "converted", outputName);
  await fs.mkdir(path.dirname(outputPath), { recursive: true });
  await fs.copyFile(rawFilePath, outputPath);
}

async function writeModel(plan: ImportPreviewPlan, rawFilePath: string): Promise<void> {
  const outputPath = path.join(plan.cachePath, "converted", "model.glb");
  await fs.mkdir(path.dirname(outputPath), { recursive: true });
  await fs.copyFile(rawFilePath, outputPath);
}

async function writePointCloud(plan: ImportPreviewPlan, rawFilePath: string): Promise<void> {
  const convertedDir = path.join(plan.cachePath, "converted");
  await fs.mkdir(convertedDir, { recursive: true });
  await fs.copyFile(rawFilePath, path.join(convertedDir, "point_cloud.ply"));

  const manifest = {
    kind: "point-cloud",
    format: "ply",
    role: "gaussian-splat-source",
    source: {
      url: plan.url,
      license: plan.licenseName
    },
    outputs: {
      ply: buildCacheUri(plan.sourceId, plan.assetId, plan.variant, "converted/point_cloud.ply")
    },
    sceneUsage: {
      gaussianSplatUri: buildCacheUri(plan.sourceId, plan.assetId, plan.variant, "converted/point_cloud.ply")
    }
  };
  await fs.writeFile(path.join(convertedDir, "point_cloud.asset.yaml"), YAML.stringify(manifest), "utf8");
}

async function writeMaterial(plan: ImportPreviewPlan, rawFilePath: string): Promise<void> {
  const textureDir = path.join(plan.cachePath, "converted", "textures");
  await fs.mkdir(textureDir, { recursive: true });
  const textureTargets = ["basecolor.png", "normal.png", "metallic_roughness.png", "ao.png"];
  for (const texture of textureTargets) {
    await fs.copyFile(rawFilePath, path.join(textureDir, texture));
  }

  const textureUri = (name: string) => buildCacheUri(plan.sourceId, plan.assetId, plan.variant, `converted/textures/${name}`);
  const material = {
    kind: "material",
    model: "pbr-metallic-roughness",
    baseColor: {
      texture: textureUri("basecolor.png"),
      colorSpace: "srgb"
    },
    normal: {
      texture: textureUri("normal.png"),
      colorSpace: "linear"
    },
    metallicRoughness: {
      texture: textureUri("metallic_roughness.png"),
      colorSpace: "linear",
      metallicChannel: "b",
      roughnessChannel: "g"
    },
    ao: {
      texture: textureUri("ao.png"),
      colorSpace: "linear"
    },
    emissive: {
      texture: null,
      colorSpace: "srgb"
    },
    defaults: {
      baseColor: [1, 1, 1, 1],
      metallic: 0,
      roughness: 0.5
    }
  };
  await fs.writeFile(path.join(plan.cachePath, "converted", "material.yaml"), YAML.stringify(material), "utf8");
}

async function writeManualNotes(plan: ImportPreviewPlan): Promise<void> {
  const notesPath = path.join(plan.cachePath, "converted", "manual-import-notes.md");
  await fs.mkdir(path.dirname(notesPath), { recursive: true });
  await fs.writeFile(notesPath, `${plan.manualNotes}\n\nSource: ${plan.url}\n`, "utf8");
}

export function actualConvertedOutputs(plan: ImportPreviewPlan) {
  return plannedConvertedOutputs(plan);
}

function urlLikePath(input: string): string {
  try {
    return new URL(input).pathname;
  } catch {
    return input;
  }
}
