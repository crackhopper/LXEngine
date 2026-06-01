import { buildServer } from "./server";

const app = await buildServer();
const host = "127.0.0.1";
const port = Number(process.env.ASSETS_DOWNLOADER_PORT ?? 4731);

await app.listen({ host, port });
console.log(`assets-downloader API listening on http://${host}:${port}`);
