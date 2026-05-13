import { createServer, type Server } from "node:http";
import { afterEach, describe, expect, it } from "vitest";
import {
  loadRuntimeState,
  runtimeStateIsReachable,
} from "../src/editor/runtime-state.js";
import { EditorClient } from "../src/editor/editor-client.js";

function listen(server: Server): Promise<number> {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      const address = server.address();
      if (!address || typeof address === "string") {
        reject(new Error("missing address"));
        return;
      }
      resolve(address.port);
    });
  });
}

function close(server: Server): Promise<void> {
  return new Promise((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });
}

describe("editor runtime discovery", () => {
  let server: Server | undefined;

  afterEach(async () => {
    process.env.LXE_EDITOR_API_TOKEN = "";
    if (server?.listening) {
      await close(server);
    }
    server = undefined;
  });

  it("loads runtime_state.yaml without mcpUrl", () => {
    const state = loadRuntimeState(`
pid: 42
httpHost: 127.0.0.1
httpPort: 3768
wsHost: 127.0.0.1
wsPort: 3768
tokenFile: /tmp/api_token.txt
startedAt: 2026-05-13-120000
`);

    expect(state).toEqual({
      pid: 42,
      httpHost: "127.0.0.1",
      httpPort: 3768,
      wsHost: "127.0.0.1",
      wsPort: 3768,
      tokenFile: "/tmp/api_token.txt",
      startedAt: "2026-05-13-120000",
    });
    expect("mcpUrl" in state).toBe(false);
  });

  it("rejects stale discovery when health probe fails", async () => {
    const reachable = await runtimeStateIsReachable({
      pid: 42,
      httpHost: "127.0.0.1",
      httpPort: 65530,
      wsHost: "127.0.0.1",
      wsPort: 65530,
      tokenFile: "/tmp/api_token.txt",
      startedAt: "2026-05-13-120000",
    });

    expect(reachable).toBe(false);
  });

  it("fetches summary from a healthy editor API", async () => {
    let authorizationHeader: string | undefined;
    server = createServer((req, res) => {
      if (req.url === "/api/state/summary") {
        authorizationHeader = req.headers.authorization;
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ sceneName: "Scene", dirty: false }));
        return;
      }
      if (req.url === "/health") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ ok: true }));
        return;
      }
      res.writeHead(404).end();
    });

    const port = await listen(server);
    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${port}`,
      bearerToken: "fake-token",
    });

    await expect(client.getSummary()).resolves.toEqual({
      sceneName: "Scene",
      dirty: false,
    });
    expect(authorizationHeader).toBe("Bearer fake-token");
  });
});
