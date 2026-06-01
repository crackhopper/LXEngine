import fs from "node:fs";
import path from "node:path";

export function findRepoRoot(startDir = process.cwd()): string {
  let current = path.resolve(startDir);
  while (true) {
    if (fs.existsSync(path.join(current, "AGENTS.md"))) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return path.resolve(startDir);
    }
    current = parent;
  }
}

export function resolveCacheRoot(env = process.env, startDir = process.cwd()): string {
  const configured = env.LXENGINE_ASSET_CACHE;
  if (configured && configured.trim().length > 0) {
    return path.resolve(configured);
  }
  return path.join(findRepoRoot(startDir), ".asset_cache");
}

export function sanitizePathSegment(value: string): string {
  const normalized = value
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return normalized.length > 0 ? normalized : "asset";
}
