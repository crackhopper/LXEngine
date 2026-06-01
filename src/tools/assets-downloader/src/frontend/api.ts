import type { CacheAssetMetadata, Catalog, ImportJob, ImportPreviewPlan, ImportPreviewRequest, ImportStartRequest } from "../shared/schema";

export async function getSources(): Promise<Catalog> {
  return requestJson<Catalog>("/api/sources");
}

export async function previewImport(body: ImportPreviewRequest): Promise<ImportPreviewPlan> {
  return requestJson<ImportPreviewPlan>("/api/import/preview", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body)
  });
}

export async function startImport(body: ImportStartRequest): Promise<ImportJob> {
  return requestJson<ImportJob>("/api/import/start", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body)
  });
}

export async function getJob(id: string): Promise<ImportJob> {
  return requestJson<ImportJob>(`/api/jobs/${id}`);
}

export async function getCache(): Promise<{ assets: CacheAssetMetadata[] }> {
  return requestJson<{ assets: CacheAssetMetadata[] }>("/api/cache");
}

async function requestJson<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, init);
  const body = await response.json();
  if (!response.ok) {
    throw new Error(body.error ?? response.statusText);
  }
  return body as T;
}
