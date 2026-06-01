import crypto from "node:crypto";
import path from "node:path";
import { buildCacheUri, buildCacheUriBase } from "../../shared/cacheUri";
import {
  Catalog,
  ImportPreviewPlan,
  ImportPreviewRequest,
  OutputKind,
  Source
} from "../../shared/schema";
import { findRecommendedById, findRecommendedByUrl } from "./sourceRegistry";
import { resolveCacheRoot, sanitizePathSegment } from "./paths";

export function createImportPreviewPlan(catalog: Catalog, request: ImportPreviewRequest): ImportPreviewPlan {
  const matched = request.sourceId && request.assetId
    ? findRecommendedById(catalog, request.sourceId, request.assetId)
    : findRecommendedByUrl(catalog, request.url);

  const fallbackSource = pickFallbackSource(catalog, request.url, request.sourceId);
  const source = matched?.source ?? fallbackSource;
  const entry = matched?.entry;
  const assetId = sanitizePathSegment(request.assetId ?? entry?.id ?? deriveAssetId(request.url));
  const variant = sanitizePathSegment(request.variant ?? entry?.variants[0] ?? "manual");
  const kind: OutputKind = request.kind ?? entry?.recipe.outputKind ?? inferKind(request.url);
  const cacheRoot = resolveCacheRoot();
  const cachePath = path.join(cacheRoot, source.id, assetId, variant);
  const cacheUriBase = buildCacheUriBase(source.id, assetId, variant);

  const userLicense = request.userConfirmedLicense;
  const licenseStatus = userLicense ? "user_confirmed" : entry?.licenseStatus ?? "unknown";
  const licenseName = userLicense?.licenseName ?? entry?.expectedLicense ?? "unknown";
  const licenseUrl = userLicense?.licenseUrl ?? entry?.licenseUrl;
  const importAllowed = licenseStatus === "verified" || licenseStatus === "user_confirmed";
  const convertedRelativePaths = plannedConvertedPaths(kind, request.url);

  return {
    previewId: crypto.randomUUID(),
    url: request.url,
    sourceId: source.id,
    sourceName: source.name,
    assetId,
    variant,
    kind,
    licenseStatus,
    licenseName,
    licenseUrl,
    importAllowed,
    cacheRoot,
    cachePath,
    cacheUriBase,
    filesToWrite: [
      "source.yaml",
      "license/license.yaml",
      "raw/source.bin",
      ...convertedRelativePaths
    ],
    manualNotes: entry?.manualNotes ?? manualNotesForKind(kind),
  };
}

export function plannedConvertedOutputs(plan: ImportPreviewPlan) {
  return plannedConvertedPaths(plan.kind, plan.url).map((relativePath) => ({
    kind: plan.kind,
    label: relativePath.replace(/^converted\//, ""),
    relativePath,
    uri: buildCacheUri(plan.sourceId, plan.assetId, plan.variant, relativePath)
  }));
}

function pickFallbackSource(catalog: Catalog, url: string, requestedSourceId?: string): Source {
  const requested = requestedSourceId ? catalog.sources.find((source) => source.id === requestedSourceId) : undefined;
  if (requested) {
    return requested;
  }
  const host = safeHost(url);
  const byHost = catalog.sources.find((source) => safeHost(source.url) === host);
  return byHost ?? {
    id: "manual",
    name: "Manual URL",
    url: "http://localhost/",
    description: "User-provided URL outside the default catalog.",
    licensePolicy: "Requires user confirmation.",
    categories: [{ id: "material", name: "Manual", recommended: [] }]
  };
}

function deriveAssetId(url: string): string {
  try {
    const parsed = new URL(url);
    const leaf = parsed.pathname.split("/").filter(Boolean).at(-1) ?? parsed.hostname;
    return leaf.replace(/\.[^.]+$/, "") || parsed.hostname;
  } catch {
    return path.basename(url).replace(/\.[^.]+$/, "") || "asset";
  }
}

function inferKind(url: string): OutputKind {
  const lower = url.toLowerCase();
  if (lower.endsWith(".hdr") || lower.endsWith(".exr")) {
    return "environment";
  }
  if (lower.endsWith(".glb") || lower.endsWith(".gltf")) {
    return "model";
  }
  if (lower.includes("pbrt") || lower.includes("mitsuba") || lower.includes("tungsten")) {
    return "manual";
  }
  return "material";
}

function plannedConvertedPaths(kind: OutputKind, url: string): string[] {
  switch (kind) {
    case "environment":
      return [url.toLowerCase().endsWith(".exr") ? "converted/environment.exr" : "converted/environment.hdr"];
    case "model":
      return ["converted/model.glb"];
    case "material":
      return [
        "converted/material.yaml",
        "converted/textures/basecolor.png",
        "converted/textures/normal.png",
        "converted/textures/metallic_roughness.png",
        "converted/textures/ao.png"
      ];
    case "manual":
      return ["converted/manual-import-notes.md"];
  }
}

function manualNotesForKind(kind: OutputKind): string {
  if (kind === "manual") {
    return "Downloaded and indexed only. First version does not convert complex scene formats into LXEngine scenes.";
  }
  return "Converted output uses first-version copy or material manifest generation.";
}

function safeHost(url: string): string {
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch {
    return "";
  }
}
