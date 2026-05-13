import { createServer, type Server } from "node:http";
import { afterEach, describe, expect, it, vi } from "vitest";
import { createMcpHttpServer, createToolHandlers } from "../src/mcp/server.js";

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

describe("mcp tool handlers", () => {
  let server: Server | undefined;

  afterEach(async () => {
    if (server?.listening) {
      await close(server);
    }
    server = undefined;
  });

  function makeInput(overrides: {
    editorOps?: Partial<Parameters<typeof createToolHandlers>[0]["editorOps"]>;
    editorClient?: Partial<
      NonNullable<Parameters<typeof createToolHandlers>[0]["editorClient"]>
    >;
    workspaceOps?: Partial<Parameters<typeof createToolHandlers>[0]["workspaceOps"]>;
  } = {}): Parameters<typeof createToolHandlers>[0] {
    return {
      editorOps: {
        start: vi.fn(async () => ({ running: true, pid: 123 })),
        stop: vi.fn(async () => ({ running: false })),
        status: vi.fn(async () => ({ running: true, pid: 123 })),
        logs: vi.fn(async () => ({ stdout: "", stderr: "" })),
        ...overrides.editorOps,
      },
      editorClient: {
        getSummary: vi.fn(async () => ({ sceneName: "Scene", dirty: false })),
        getSelection: vi.fn(async () => ({ selectedNodeId: 7 })),
        getCameras: vi.fn(async () => ({ active: "editor_cam" })),
        pick: vi.fn(async () => ({ hit: true })),
        command: vi.fn(async () => ({ ok: true })),
        waitFor: vi.fn(async () => ({ found: true })),
        ...overrides.editorClient,
      },
      workspaceOps: {
        repoPull: vi.fn(async () => ({ exitCode: 0 })),
        buildConfigure: vi.fn(async () => ({ exitCode: 0 })),
        buildTarget: vi.fn(async () => ({ exitCode: 0 })),
        ...overrides.workspaceOps,
      },
    };
  }

  it("routes editor summary to the editor client", async () => {
    const getSummary = vi.fn(async () => ({
      sceneName: "Scene",
      dirty: false,
    }));
    const handlers = createToolHandlers(makeInput({ editorClient: { getSummary } }));

    await expect(handlers["editor.get_summary"]({})).resolves.toEqual({
      content: [{ type: "text", text: '{"sceneName":"Scene","dirty":false}' }],
    });
    expect(getSummary).toHaveBeenCalledOnce();
  });

  it("routes repo pull to workspace ops", async () => {
    const repoPull = vi.fn(async () => ({
      exitCode: 0,
      stdout: "ok",
      stderr: "",
    }));
    const handlers = createToolHandlers(makeInput({ workspaceOps: { repoPull } }));

    await expect(handlers["ops.repo_pull"]({})).resolves.toEqual({
      content: [
        {
          type: "text",
          text: '{"exitCode":0,"stdout":"ok","stderr":""}',
        },
      ],
    });
    expect(repoPull).toHaveBeenCalledOnce();
  });

  it("returns a structured guardian error when a task is killed", async () => {
    const handlers = createToolHandlers(makeInput({
      workspaceOps: {
        repoPull: async () => {
          throw new Error("killed_by_guardian: cpu=390 rss=4096 free_mem=16");
        },
      },
    }));

    await expect(handlers["ops.repo_pull"]({})).rejects.toThrow(
      "ops.repo_pull failed: killed_by_guardian: cpu=390 rss=4096 free_mem=16",
    );
  });

  it("exposes the accepted tool surface", () => {
    const handlers = createToolHandlers(makeInput());

    expect(Object.keys(handlers).sort()).toEqual([
      "editor.command",
      "editor.get_cameras",
      "editor.get_selection",
      "editor.get_summary",
      "editor.pick",
      "editor.wait_for",
      "ops.build_configure",
      "ops.build_target",
      "ops.editor_logs",
      "ops.editor_start",
      "ops.editor_status",
      "ops.editor_stop",
      "ops.repo_pull",
    ]);
  });

  it("routes new editor and ops tools to their handlers", async () => {
    const getSelection = vi.fn(async () => ({ selectedNodeId: 7 }));
    const getCameras = vi.fn(async () => ({ active: "editor_cam" }));
    const pick = vi.fn(async () => ({ hit: true }));
    const command = vi.fn(async () => ({ ok: true }));
    const waitFor = vi.fn(async () => ({ found: true }));
    const buildConfigure = vi.fn(async () => ({ exitCode: 0, label: "build.configure" }));
    const buildTarget = vi.fn(async () => ({ exitCode: 0, label: "build.target" }));
    const start = vi.fn(async () => ({ running: true, pid: 321 }));
    const stop = vi.fn(async () => ({ running: false }));
    const logs = vi.fn(async () => ({ stdout: "out", stderr: "err" }));
    const handlers = createToolHandlers(
      makeInput({
        editorClient: { getSelection, getCameras, pick, command, waitFor },
        editorOps: { start, stop, logs },
        workspaceOps: { buildConfigure, buildTarget },
      }),
    );

    await handlers["editor.get_selection"]({});
    await handlers["editor.get_cameras"]({});
    await handlers["editor.pick"]({ x: 11, y: 22 });
    await handlers["editor.command"]({ line: "scene list" });
    await handlers["editor.wait_for"]({ contains: "Scene" });
    await handlers["ops.build_configure"]({ buildDir: "/tmp/build" });
    await handlers["ops.build_target"]({ buildDir: "/tmp/build", target: "lxe_editor" });
    await handlers["ops.editor_start"]({});
    await handlers["ops.editor_stop"]({});
    await handlers["ops.editor_logs"]({});

    expect(getSelection).toHaveBeenCalledOnce();
    expect(getCameras).toHaveBeenCalledOnce();
    expect(pick).toHaveBeenCalledWith(11, 22);
    expect(command).toHaveBeenCalledWith("scene list");
    expect(waitFor).toHaveBeenCalledWith({ contains: "Scene" });
    expect(buildConfigure).toHaveBeenCalledWith("/tmp/build");
    expect(buildTarget).toHaveBeenCalledWith("/tmp/build", "lxe_editor");
    expect(start).toHaveBeenCalledOnce();
    expect(stop).toHaveBeenCalledOnce();
    expect(logs).toHaveBeenCalledOnce();
  });

  it("returns a clear editor unavailable tool error without discovery", async () => {
    const handlers = createToolHandlers({
      ...makeInput(),
      editorClient: undefined,
    });

    await expect(handlers["editor.get_summary"]({})).resolves.toEqual({
      isError: true,
      content: [
        {
          type: "text",
          text: '{"ok":false,"error":{"code":"editor_unavailable","message":"lxe_editor HTTP API is unavailable: runtime_state.yaml and token file were not discovered"}}',
        },
      ],
    });
  });

  it("serves tools/list and tools/call over HTTP JSON-RPC at /mcp", async () => {
    const handlers = createToolHandlers(makeInput());
    server = createMcpHttpServer({ handlers });
    const port = await listen(server);
    const response = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 1,
        method: "tools/list",
      }),
    });

    await expect(response.json()).resolves.toMatchObject({
      jsonrpc: "2.0",
      id: 1,
      result: {
        tools: expect.arrayContaining([
          expect.objectContaining({ name: "editor.get_summary" }),
          expect.objectContaining({ name: "ops.editor_logs" }),
        ]),
      },
    });

    const callResponse = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 2,
        method: "tools/call",
        params: {
          name: "editor.get_summary",
          arguments: {},
        },
      }),
    });

    await expect(callResponse.json()).resolves.toEqual({
      jsonrpc: "2.0",
      id: 2,
      result: {
        content: [{ type: "text", text: '{"sceneName":"Scene","dirty":false}' }],
      },
    });
  });
});
