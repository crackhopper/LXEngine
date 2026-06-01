export function buildCacheUri(sourceId: string, assetId: string, variant: string, relativeConvertedPath: string): string {
  const cleaned = relativeConvertedPath.replace(/^\/+/, "");
  if (!cleaned.startsWith("converted/")) {
    throw new Error(`cache:// URI can only target converted output: ${relativeConvertedPath}`);
  }
  return `cache://${sourceId}/${assetId}/${variant}/${cleaned}`;
}

export function buildCacheUriBase(sourceId: string, assetId: string, variant: string): string {
  return `cache://${sourceId}/${assetId}/${variant}`;
}

export function parseCacheUri(uri: string): { sourceId: string; assetId: string; variant: string; relativePath: string } {
  const match = /^cache:\/\/([^/]+)\/([^/]+)\/([^/]+)\/(.+)$/.exec(uri);
  if (!match) {
    throw new Error(`Invalid cache URI: ${uri}`);
  }
  const [, sourceId, assetId, variant, relativePath] = match;
  if (!relativePath.startsWith("converted/")) {
    throw new Error(`cache:// URI cannot target raw files: ${uri}`);
  }
  return { sourceId, assetId, variant, relativePath };
}
