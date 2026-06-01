import { useEffect, useMemo, useState } from "react";
import { getCache, getJob, getSources, previewImport, startImport } from "./api";
import type { CacheAssetMetadata, Catalog, ImportJob, ImportPreviewPlan, RecommendedAsset, Source } from "../shared/schema";
import "./styles.css";

export function App() {
  const [catalog, setCatalog] = useState<Catalog>();
  const [activeSourceId, setActiveSourceId] = useState<string>();
  const [activeCategory, setActiveCategory] = useState<string>("all");
  const [importUrl, setImportUrl] = useState("");
  const [selectedEntry, setSelectedEntry] = useState<RecommendedAsset>();
  const [preview, setPreview] = useState<ImportPreviewPlan>();
  const [job, setJob] = useState<ImportJob>();
  const [cacheAssets, setCacheAssets] = useState<CacheAssetMetadata[]>([]);
  const [error, setError] = useState<string>();

  useEffect(() => {
    void getSources()
      .then((loaded) => {
        setCatalog(loaded);
        setActiveSourceId(loaded.sources[0]?.id);
      })
      .catch((caught: Error) => setError(caught.message));
    void refreshCache();
  }, []);

  useEffect(() => {
    if (!job || job.status === "completed" || job.status === "failed") {
      if (job?.status === "completed") {
        void refreshCache();
      }
      return;
    }
    const timer = window.setInterval(() => {
      void getJob(job.id).then(setJob).catch((caught: Error) => setError(caught.message));
    }, 800);
    return () => window.clearInterval(timer);
  }, [job]);

  const source = useMemo(() => catalog?.sources.find((candidate) => candidate.id === activeSourceId), [catalog, activeSourceId]);
  const categories = source?.categories ?? [];
  const recommendations = useMemo(() => {
    const entries = categories.flatMap((category) =>
      category.recommended.map((entry) => ({ ...entry, categoryId: category.id, categoryName: category.name }))
    );
    return activeCategory === "all" ? entries : entries.filter((entry) => entry.categoryId === activeCategory);
  }, [categories, activeCategory]);

  async function refreshCache() {
    const cache = await getCache();
    setCacheAssets(cache.assets);
  }

  async function createPreview(entry = selectedEntry) {
    setError(undefined);
    const body = {
      url: importUrl,
      sourceId: source?.id,
      assetId: entry?.id,
      variant: entry?.variants[0],
      kind: entry?.recipe.outputKind
    };
    const nextPreview = await previewImport(body);
    setPreview(nextPreview);
  }

  async function runImport() {
    if (!preview) {
      return;
    }
    setError(undefined);
    const nextJob = await startImport({
      url: preview.url,
      sourceId: preview.sourceId,
      assetId: preview.assetId,
      variant: preview.variant,
      kind: preview.kind
    });
    setJob(nextJob);
  }

  function chooseSource(nextSource: Source) {
    setActiveSourceId(nextSource.id);
    setActiveCategory("all");
    setSelectedEntry(undefined);
    setPreview(undefined);
    setImportUrl("");
  }

  function chooseEntry(entry: RecommendedAsset) {
    setSelectedEntry(entry);
    setImportUrl(entry.url);
    setPreview(undefined);
  }

  return (
    <main className="shell">
      <aside className="sources" aria-label="Data source navigation">
        <h1>Assets Downloader</h1>
        <nav>
          {catalog?.sources.map((candidate) => (
            <button
              className={candidate.id === activeSourceId ? "active" : ""}
              key={candidate.id}
              onClick={() => chooseSource(candidate)}
            >
              {candidate.name}
            </button>
          ))}
        </nav>
      </aside>

      <section className="workspace">
        {source && (
          <section className="source-detail">
            <div>
              <h2>{source.name}</h2>
              <p>{source.description}</p>
              <p className="license">License: {source.licensePolicy}</p>
            </div>
            <a href={source.url} target="_blank" rel="noreferrer">Open original site</a>
          </section>
        )}

        <section className="filters" aria-label="Filters">
          <button className={activeCategory === "all" ? "active" : ""} onClick={() => setActiveCategory("all")}>All</button>
          {categories.map((category) => (
            <button
              className={activeCategory === category.id ? "active" : ""}
              key={category.id}
              onClick={() => setActiveCategory(category.id)}
            >
              {category.name}
            </button>
          ))}
        </section>

        <section className="layout">
          <section className="panel">
            <h3>Recommended URLs</h3>
            <div className="recommendations">
              {recommendations.length === 0 && <p className="empty">No verified default import URL for this source yet.</p>}
              {recommendations.map((entry) => (
                <article className="recommendation" key={entry.id}>
                  <div>
                    <strong>{entry.displayName}</strong>
                    <span>{entry.kind} · {entry.variants.join(", ")}</span>
                    <span className="license-state">{entry.licenseStatus} · {entry.expectedLicense}</span>
                  </div>
                  <div className="actions">
                    <a href={entry.sourceUrl} target="_blank" rel="noreferrer">Original</a>
                    <button onClick={() => chooseEntry(entry)}>Use URL</button>
                  </div>
                </article>
              ))}
            </div>
          </section>

          <section className="panel import-tool">
            <h3>Import Tool</h3>
            <label>
              URL
              <input value={importUrl} onChange={(event) => setImportUrl(event.target.value)} placeholder="https://..." />
            </label>
            <button disabled={!importUrl} onClick={() => void createPreview()}>Preview Import</button>
            {error && <p className="error">{error}</p>}
            {preview && (
              <section className="preview">
                <h4>Preview Plan</h4>
                <dl>
                  <dt>Source</dt><dd>{preview.sourceName}</dd>
                  <dt>License</dt><dd>{preview.licenseStatus} · {preview.licenseName}</dd>
                  <dt>Cache path</dt><dd>{preview.cachePath}</dd>
                  <dt>URI base</dt><dd>{preview.cacheUriBase}</dd>
                </dl>
                <ul>
                  {preview.filesToWrite.map((file) => <li key={file}>{file}</li>)}
                </ul>
                <button disabled={!preview.importAllowed} onClick={() => void runImport()}>Start Import</button>
                {!preview.importAllowed && <p className="blocked">Import requires verified or user-confirmed license state.</p>}
              </section>
            )}
          </section>
        </section>

        <section className="layout">
          <section className="panel">
            <h3>Job Log</h3>
            {job ? (
              <div>
                <p>Status: <strong>{job.status}</strong></p>
                <pre>{job.logs.join("\n")}</pre>
                {job.error && <p className="error">{job.error}</p>}
              </div>
            ) : <p className="empty">No job started.</p>}
          </section>

          <section className="panel">
            <h3>Cache Browser</h3>
            {cacheAssets.length === 0 && <p className="empty">No cache assets found.</p>}
            {cacheAssets.map((asset) => (
              <article className="cache-asset" key={asset.asset.cacheUriBase}>
                <strong>{asset.displayName}</strong>
                <span>{asset.kind} · {asset.license.status}</span>
                <code>{asset.asset.cacheUriBase}</code>
              </article>
            ))}
          </section>
        </section>
      </section>
    </main>
  );
}
