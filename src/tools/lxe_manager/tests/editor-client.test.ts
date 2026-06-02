import { createServer, type Server } from "node:http";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  discoverEditorClientConfig,
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
  const originalApiToken = process.env.LXE_EDITOR_API_TOKEN;

  afterEach(async () => {
    if (originalApiToken === undefined) {
      delete process.env.LXE_EDITOR_API_TOKEN;
    } else {
      process.env.LXE_EDITOR_API_TOKEN = originalApiToken;
    }
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

  it("rejects malformed runtime_state.yaml", () => {
    expect(() =>
      loadRuntimeState(`
pid: 42
httpHost: 127.0.0.1
httpPort: not-a-port
wsHost: 127.0.0.1
wsPort: 3768
tokenFile: /tmp/api_token.txt
startedAt: 2026-05-13-120000
`),
    ).toThrow("runtime state invalid httpPort");
  });

  it("discovers editor client config from runtime_state.yaml and token file", () => {
    const root = mkdtempSync(path.join(tmpdir(), "lxe-runtime-"));
    const tokenFile = path.join(root, "api_token.txt");
    const stateFile = path.join(root, "runtime_state.yaml");
    writeFileSync(tokenFile, "secret-token\n");
    writeFileSync(
      stateFile,
      `
pid: 42
httpHost: 127.0.0.1
httpPort: 3768
wsHost: 127.0.0.1
wsPort: 3768
tokenFile: ${tokenFile}
startedAt: 2026-05-13-120000
`,
    );

    expect(discoverEditorClientConfig(stateFile)).toMatchObject({
      httpBaseUrl: "http://127.0.0.1:3768",
      bearerToken: "secret-token",
      state: {
        pid: 42,
        tokenFile,
      },
    });
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

  it("rejects a stalled health probe after timeout", async () => {
    server = createServer(() => {
      // Leave the request open to exercise AbortSignal.timeout.
    });

    const port = await listen(server);
    const reachable = await runtimeStateIsReachable(
      {
        pid: 42,
        httpHost: "127.0.0.1",
        httpPort: port,
        wsHost: "127.0.0.1",
        wsPort: port,
        tokenFile: "/tmp/api_token.txt",
        startedAt: "2026-05-13-120000",
      },
      10,
    );

    expect(reachable).toBe(false);
  });

  it("fetches summary from a healthy editor API", async () => {
    const requests: Array<{ url: string | undefined; authorization?: string }> = [];
    server = createServer((req, res) => {
      requests.push({ url: req.url, authorization: req.headers.authorization });
      if (req.url === "/api/state/summary") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ sceneName: "Scene", dirty: false }));
        return;
      }
      if (req.url === "/api/state/selection") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ selectedNodeId: 7 }));
        return;
      }
      if (req.url === "/api/state/cameras") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ active: "editor_cam" }));
        return;
      }
      if (req.url === "/api/build") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(
          JSON.stringify({ buildInfo: "lxe_editor 0.1.0-dev (0123456789ab, Debug, Linux-x64)" }),
        );
        return;
      }
      if (req.url === "/api/pick") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ hit: true }));
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
    await expect(client.getSelection()).resolves.toEqual({ selectedNodeId: 7 });
    await expect(client.getCameras()).resolves.toEqual({ active: "editor_cam" });
    await expect(client.buildInfo()).resolves.toEqual({ buildInfo: "lxe_editor 0.1.0-dev (0123456789ab, Debug, Linux-x64)" });
    await expect(client.pick(4, 8)).resolves.toEqual({ hit: true });
    expect(requests.map((request) => request.url)).toEqual([
      "/api/state/summary",
      "/api/state/selection",
      "/api/state/cameras",
      "/api/build",
      "/api/pick",
    ]);
    expect(requests.every((request) => request.authorization === "Bearer fake-token")).toBe(
      true,
    );
  });

  it("forwards recording API requests to the editor", async () => {
    const requests: Array<{
      method: string | undefined;
      url: string | undefined;
      body?: unknown;
    }> = [];
    server = createServer(async (req, res) => {
      const chunks: Buffer[] = [];
      for await (const chunk of req) {
        chunks.push(Buffer.from(chunk));
      }
      const text = Buffer.concat(chunks).toString("utf8");
      requests.push({
        method: req.method,
        url: req.url,
        body: text.length === 0 ? undefined : JSON.parse(text),
      });
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ ok: true, path: req.url }));
    });

    const port = await listen(server);
    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${port}`,
      bearerToken: "fake-token",
    });

    await expect(client.recordingStatus()).resolves.toMatchObject({ ok: true });
    await expect(client.recordingEnable()).resolves.toMatchObject({ ok: true });
    await expect(client.recordingDisable({ force: true })).resolves.toMatchObject({
      ok: true,
    });
    await expect(client.recordingStart({ detailLevel: "diagnostic" })).resolves.toMatchObject({
      ok: true,
    });
    await expect(client.recordingStop({ save: false })).resolves.toMatchObject({ ok: true });
    await expect(client.recordingList()).resolves.toMatchObject({ ok: true });
    await expect(client.recordingRead("session-1")).resolves.toMatchObject({ ok: true });
    await expect(client.recordingReplay({ id: "session-1" })).resolves.toMatchObject({
      ok: true,
    });
    await expect(client.recordingProbe("summary")).resolves.toMatchObject({ ok: true });

    expect(requests).toEqual([
      { method: "GET", url: "/recording/status", body: undefined },
      { method: "POST", url: "/recording/enable", body: undefined },
      { method: "POST", url: "/recording/disable", body: { force: true } },
      { method: "POST", url: "/recording/start", body: { detailLevel: "diagnostic" } },
      { method: "POST", url: "/recording/stop", body: { save: false } },
      { method: "GET", url: "/recording/list", body: undefined },
      { method: "GET", url: "/recording/read?id=session-1", body: undefined },
      { method: "POST", url: "/recording/replay", body: { id: "session-1" } },
      { method: "GET", url: "/recording/probe?target=summary", body: undefined },
    ]);
  });

  it("forwards display API requests to the editor", async () => {
    const requests: Array<{
      method: string | undefined;
      url: string | undefined;
      body?: unknown;
    }> = [];
    server = createServer(async (req, res) => {
      const chunks: Buffer[] = [];
      for await (const chunk of req) {
        chunks.push(Buffer.from(chunk));
      }
      const text = Buffer.concat(chunks).toString("utf8");
      requests.push({
        method: req.method,
        url: req.url,
        body: text.length === 0 ? undefined : JSON.parse(text),
      });
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ ok: true, path: req.url }));
    });

    const port = await listen(server);
    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${port}`,
      bearerToken: "fake-token",
    });

    await expect(client.displayList()).resolves.toMatchObject({ ok: true });
    await expect(client.displayActive()).resolves.toMatchObject({ ok: true });
    await expect(client.displayConfigGet("active profile")).resolves.toMatchObject({
      ok: true,
    });
    await expect(
      client.displayConfigSet({ key: "desktop", patch: "width: 1280" }),
    ).resolves.toMatchObject({ ok: true });
    await expect(client.displaySelect("desktop")).resolves.toMatchObject({ ok: true });

    expect(requests).toEqual([
      { method: "GET", url: "/api/display/list", body: undefined },
      { method: "GET", url: "/api/display/active", body: undefined },
      {
        method: "GET",
        url: "/api/display/config?key=active%20profile",
        body: undefined,
      },
      {
        method: "POST",
        url: "/api/display/config",
        body: { key: "desktop", patch: "width: 1280" },
      },
      { method: "POST", url: "/api/display/select", body: { key: "desktop" } },
    ]);
  });

  it("aborts editor requests after timeout", async () => {
    server = createServer(() => {
      // Leave the request open to exercise the client timeout.
    });

    const port = await listen(server);
    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${port}`,
      timeoutMs: 10,
    });

    await expect(client.getSummary()).rejects.toThrow();
  });

  it("retries transient GET failures", async () => {
    let requests = 0;
    server = createServer((req, res) => {
      ++requests;
      if (requests === 1) {
        req.socket.destroy();
        return;
      }
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ sceneName: "Recovered" }));
    });

    const port = await listen(server);
    const client = new EditorClient({
      httpBaseUrl: `http://127.0.0.1:${port}`,
    });

    await expect(client.getSummary()).resolves.toEqual({
      sceneName: "Recovered",
    });
    expect(requests).toBe(2);
  });
});
