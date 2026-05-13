import { mkdirSync, writeFileSync } from "node:fs";
import { mkdtempSync } from "node:fs";
import { createServer, type Server } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { defaultRepoRoot, resolveManagerConfig } from "../src/config.js";
import {
  discoverReachableEditorClientConfig,
  loadRuntimeState,
} from "../src/editor/runtime-state.js";

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

describe("resolveManagerConfig", () => {
  let server: Server | undefined;

  afterEach(async () => {
    if (server?.listening) {
      await close(server);
    }
    server = undefined;
  });

  it("builds default repo-local paths for the workspace", () => {
    const config = resolveManagerConfig({
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
    });

    expect(config.repoRoot).toBe("/repo");
    expect(config.runtimeStatePath).toBe(
      path.join("/runtime", "data", "lxe_editor", "runtime_state.yaml"),
    );
    expect(config.editorExecutable).toBe(
      path.join(
        "/repo",
        "build",
        "src",
        "demos",
        "lxe_editor",
        process.platform === "win32" ? "lxe_editor.exe" : "lxe_editor",
      ),
    );
  });

  it("allows an explicit editor executable override", () => {
    const config = resolveManagerConfig({
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
      editorExecutable: "/custom/lxe_editor",
    });

    expect(config.editorExecutable).toBe("/custom/lxe_editor");
  });

  it("resolves the default repo root from the manager source location", () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-repo-"));
    mkdirSync(path.join(repoRoot, "tools", "lxe_manager", "src"), {
      recursive: true,
    });
    writeFileSync(path.join(repoRoot, "AGENTS.md"), "");
    writeFileSync(path.join(repoRoot, "CMakeLists.txt"), "");
    const sourceUrl = new URL(
      `file://${path.join(repoRoot, "tools", "lxe_manager", "src", "index.ts")}`,
    );

    expect(defaultRepoRoot(sourceUrl.href)).toBe(repoRoot);
  });

  it("resolves the default repo root from a compiled dist location", () => {
    const repoRoot = mkdtempSync(path.join(tmpdir(), "lxe-manager-repo-"));
    mkdirSync(path.join(repoRoot, "tools", "lxe_manager", "dist", "src"), {
      recursive: true,
    });
    writeFileSync(path.join(repoRoot, "AGENTS.md"), "");
    writeFileSync(path.join(repoRoot, "CMakeLists.txt"), "");
    const distUrl = new URL(
      `file://${path.join(
        repoRoot,
        "tools",
        "lxe_manager",
        "dist",
        "src",
        "index.js",
      )}`,
    );

    expect(defaultRepoRoot(distUrl.href)).toBe(repoRoot);
  });

  it("rejects stale runtime state when the editor health endpoint is unreachable", async () => {
    const tempRoot = mkdtempSync(path.join(tmpdir(), "lxe-runtime-"));
    const tokenPath = path.join(tempRoot, "api_token.txt");
    const statePath = path.join(tempRoot, "runtime_state.yaml");
    writeFileSync(tokenPath, "secret\n");
    writeFileSync(
      statePath,
      [
        "pid: 1234",
        "httpHost: 127.0.0.1",
        "httpPort: 9",
        "wsHost: 127.0.0.1",
        "wsPort: 3001",
        `tokenFile: ${tokenPath}`,
        "startedAt: 2026-05-13T00:00:00Z",
      ].join("\n"),
    );

    await expect(discoverReachableEditorClientConfig(statePath, 20)).resolves.toBeUndefined();
  });

  it("accepts runtime state when the editor health endpoint responds", async () => {
    server = createServer((_request, response) => {
      response.writeHead(200, { "content-type": "application/json" });
      response.end('{"ok":true}');
    });
    const port = await listen(server);
    const tempRoot = mkdtempSync(path.join(tmpdir(), "lxe-runtime-"));
    const tokenPath = path.join(tempRoot, "api_token.txt");
    const statePath = path.join(tempRoot, "runtime_state.yaml");
    writeFileSync(tokenPath, "secret\n");
    writeFileSync(
      statePath,
      [
        "pid: 1234",
        "httpHost: 127.0.0.1",
        `httpPort: ${port}`,
        "wsHost: 127.0.0.1",
        "wsPort: 3001",
        `tokenFile: ${tokenPath}`,
        "startedAt: 2026-05-13T00:00:00Z",
      ].join("\n"),
    );

    await expect(discoverReachableEditorClientConfig(statePath, 100)).resolves.toMatchObject({
      httpBaseUrl: `http://127.0.0.1:${port}`,
      bearerToken: "secret",
      state: loadRuntimeState(
        [
          "pid: 1234",
          "httpHost: 127.0.0.1",
          `httpPort: ${port}`,
          "wsHost: 127.0.0.1",
          "wsPort: 3001",
          `tokenFile: ${tokenPath}`,
          "startedAt: 2026-05-13T00:00:00Z",
        ].join("\n"),
      ),
    });
  });
});
