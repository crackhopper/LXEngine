import cors from "@fastify/cors";
import Fastify, { FastifyError } from "fastify";
import {
  importPreviewRequestSchema,
  importStartRequestSchema
} from "../shared/schema";
import { listCacheAssets } from "./services/cacheIndexer";
import { createImportPreviewPlan } from "./services/importPlanner";
import { JobStore } from "./services/jobStore";
import { loadCatalog } from "./services/sourceRegistry";

export async function buildServer() {
  const app = Fastify({ logger: false });
  const jobs = new JobStore();

  await app.register(cors, {
    origin: ["http://127.0.0.1:5173", "http://localhost:5173"]
  });

  app.get("/api/sources", async () => {
    return loadCatalog();
  });

  app.post("/api/import/preview", async (request, reply) => {
    const body = importPreviewRequestSchema.parse(request.body);
    const catalog = await loadCatalog();
    return reply.send(createImportPreviewPlan(catalog, body));
  });

  app.post("/api/import/start", async (request, reply) => {
    const body = importStartRequestSchema.parse(request.body);
    const catalog = await loadCatalog();
    const plan = createImportPreviewPlan(catalog, body);
    return reply.send(jobs.start(plan, body));
  });

  app.get<{ Params: { id: string } }>("/api/jobs/:id", async (request, reply) => {
    const job = jobs.get(request.params.id);
    if (!job) {
      return reply.code(404).send({ error: "job not found" });
    }
    return reply.send(job);
  });

  app.get("/api/cache", async () => {
    return { assets: await listCacheAssets() };
  });

  app.setErrorHandler((error: FastifyError, _request, reply) => {
    const statusCode = "statusCode" in error && typeof error.statusCode === "number" ? error.statusCode : 400;
    return reply.code(statusCode).send({ error: error.message });
  });

  return app;
}
