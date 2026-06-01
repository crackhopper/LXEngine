import fs from "node:fs/promises";
import path from "node:path";
import YAML from "yaml";
import { Catalog, RecommendedAsset, Source, catalogSchema } from "../../shared/schema";

export async function loadCatalog(catalogPath = path.resolve(process.cwd(), "catalog/default.yaml")): Promise<Catalog> {
  const raw = await fs.readFile(catalogPath, "utf8");
  const parsed = YAML.parse(raw);
  const catalog = catalogSchema.parse(parsed);
  validateDefaultCatalog(catalog);
  return catalog;
}

export function validateDefaultCatalog(catalog: Catalog): void {
  const seenSourceIds = new Set<string>();
  for (const source of catalog.sources) {
    if (seenSourceIds.has(source.id)) {
      throw new Error(`Duplicate source id: ${source.id}`);
    }
    seenSourceIds.add(source.id);
    for (const category of source.categories) {
      for (const entry of category.recommended) {
        if (entry.licenseStatus !== "verified") {
          throw new Error(`Default recommended asset must be verified: ${source.id}/${entry.id}`);
        }
        if (!entry.recipe) {
          throw new Error(`Default recommended asset requires recipe: ${source.id}/${entry.id}`);
        }
      }
    }
  }
}

export function findRecommendedByUrl(catalog: Catalog, url: string): { source: Source; entry: RecommendedAsset } | undefined {
  for (const source of catalog.sources) {
    for (const category of source.categories) {
      const entry = category.recommended.find((candidate) => candidate.url === url || candidate.sourceUrl === url);
      if (entry) {
        return { source, entry };
      }
    }
  }
  return undefined;
}

export function findRecommendedById(
  catalog: Catalog,
  sourceId: string,
  assetId: string
): { source: Source; entry: RecommendedAsset } | undefined {
  const source = catalog.sources.find((candidate) => candidate.id === sourceId);
  if (!source) {
    return undefined;
  }
  for (const category of source.categories) {
    const entry = category.recommended.find((candidate) => candidate.id === assetId);
    if (entry) {
      return { source, entry };
    }
  }
  return undefined;
}
