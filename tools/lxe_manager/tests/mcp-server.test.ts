import { createServer, type Server } from "node:http";
import { afterEach, describe, expect, it, vi } from "vitest";
import {
  createMcpHttpServer,
  createResourceHandlers,
  createToolHandlers,
} from "../src/mcp/server.js";

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
        restart: vi.fn(async () => ({ running: true, pid: 456 })),
        status: vi.fn(async () => ({ running: true, pid: 123 })),
        logs: vi.fn(async () => ({ stdout: "", stderr: "" })),
        ...overrides.editorOps,
      },
      editorClient: {
        health: vi.fn(async () => ({ ok: true })),
        getSummary: vi.fn(async () => ({ sceneName: "Scene", dirty: false })),
        getSelection: vi.fn(async () => ({ selectedNodeId: 7 })),
        getCameras: vi.fn(async () => ({ active: "editor_cam" })),
        getToolbar: vi.fn(async () => ({ activeTool: "select" })),
        getScene: vi.fn(async () => ({ nodes: [{ id: 7, name: "Cube" }] })),
        buildInfo: vi.fn(async () => ({
          gitCommit: "0123456789abcdef",
          gitCommitShort: "0123456789ab",
          gitDirty: false,
        })),
        pick: vi.fn(async () => ({ hit: true })),
        command: vi.fn(async () => ({ ok: true })),
        waitFor: vi.fn(async () => ({ found: true })),
        recordingStatus: vi.fn(async () => ({ enabled: false })),
        recordingEnable: vi.fn(async () => ({ enabled: true })),
        recordingDisable: vi.fn(async () => ({ enabled: false })),
        recordingStart: vi.fn(async () => ({ sessionId: "session-1" })),
        recordingStop: vi.fn(async () => ({ saved: true })),
        recordingList: vi.fn(async () => ({ recordings: [] })),
        recordingRead: vi.fn(async () => ({ id: "session-1" })),
        recordingReplay: vi.fn(async () => ({ ok: true })),
        recordingProbe: vi.fn(async () => ({ target: "summary" })),
        displayList: vi.fn(async () => ({ profiles: [] })),
        displayActive: vi.fn(async () => ({ key: "active" })),
        displayConfigGet: vi.fn(async () => ({ key: "active", config: {} })),
        displayConfigSet: vi.fn(async () => ({ ok: true })),
        displaySelect: vi.fn(async () => ({ key: "desktop" })),
        ...overrides.editorClient,
      },
      workspaceOps: {
        repoPull: vi.fn(async () => ({ exitCode: 0 })),
        buildConfigure: vi.fn(async () => ({ exitCode: 0 })),
        buildTarget: vi.fn(async () => ({ exitCode: 0 })),
        buildState: vi.fn(async () => ({ repoHeadShort: "0123456789ab" })),
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

  it("routes legacy lxe_editor summary to the editor client", async () => {
    const getSummary = vi.fn(async () => ({
      sceneName: "Scene",
      dirty: false,
    }));
    const handlers = createToolHandlers(makeInput({ editorClient: { getSummary } }));

    await expect(handlers["lxe_editor_get_summary"]({})).resolves.toEqual({
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
      "display_active",
      "display_config_get",
      "display_config_set",
      "display_list",
      "display_select",
      "editor.command",
      "editor.get_build_info",
      "editor.get_cameras",
      "editor.get_selection",
      "editor.get_summary",
      "editor.pick",
      "editor.wait_for",
      "lxe_editor_command",
      "lxe_editor_ensure_running",
      "lxe_editor_get_build_info",
      "lxe_editor_get_cameras",
      "lxe_editor_get_selection",
      "lxe_editor_get_summary",
      "lxe_editor_pick",
      "lxe_editor_wait_for",
      "ops.build_configure",
      "ops.build_state",
      "ops.build_target",
      "ops.editor_logs",
      "ops.editor_restart",
      "ops.editor_start",
      "ops.editor_status",
      "ops.editor_stop",
      "ops.repo_pull",
      "recording_disable",
      "recording_enable",
      "recording_list",
      "recording_probe",
      "recording_read",
      "recording_replay",
      "recording_start",
      "recording_status",
      "recording_stop",
    ]);
  });

  it("routes recording tools to the editor client", async () => {
    const recordingStatus = vi.fn(async () => ({ enabled: false }));
    const recordingEnable = vi.fn(async () => ({ enabled: true }));
    const recordingDisable = vi.fn(async () => ({ enabled: false }));
    const recordingStart = vi.fn(async () => ({ sessionId: "session-1" }));
    const recordingStop = vi.fn(async () => ({ saved: false }));
    const recordingList = vi.fn(async () => ({ recordings: [] }));
    const recordingRead = vi.fn(async () => ({ id: "session-1" }));
    const recordingReplay = vi.fn(async () => ({ ok: true }));
    const recordingProbe = vi.fn(async () => ({ target: "summary" }));
    const handlers = createToolHandlers(
      makeInput({
        editorClient: {
          recordingStatus,
          recordingEnable,
          recordingDisable,
          recordingStart,
          recordingStop,
          recordingList,
          recordingRead,
          recordingReplay,
          recordingProbe,
        },
      }),
    );

    await handlers.recording_status({});
    await handlers.recording_enable({});
    await handlers.recording_disable({ force: true });
    await handlers.recording_start({ detailLevel: "trace" });
    await handlers.recording_stop({ save: false });
    await handlers.recording_list({});
    await handlers.recording_read({ id: "session-1" });
    await handlers.recording_replay({ path: "/tmp/recording.json" });
    await handlers.recording_probe({ target: "selection" });

    expect(recordingStatus).toHaveBeenCalledOnce();
    expect(recordingEnable).toHaveBeenCalledOnce();
    expect(recordingDisable).toHaveBeenCalledWith({ force: true });
    expect(recordingStart).toHaveBeenCalledWith({ detailLevel: "trace" });
    expect(recordingStop).toHaveBeenCalledWith({ save: false });
    expect(recordingList).toHaveBeenCalledOnce();
    expect(recordingRead).toHaveBeenCalledWith("session-1");
    expect(recordingReplay).toHaveBeenCalledWith({ path: "/tmp/recording.json" });
    expect(recordingProbe).toHaveBeenCalledWith("selection");
  });

  it("routes display tools to the editor client", async () => {
    const displayList = vi.fn(async () => ({ profiles: ["desktop"] }));
    const displayActive = vi.fn(async () => ({ key: "desktop" }));
    const displayConfigGet = vi.fn(async () => ({ key: "desktop", config: {} }));
    const displayConfigSet = vi.fn(async () => ({ ok: true }));
    const displaySelect = vi.fn(async () => ({ key: "desktop" }));
    const handlers = createToolHandlers(
      makeInput({
        editorClient: {
          displayList,
          displayActive,
          displayConfigGet,
          displayConfigSet,
          displaySelect,
        },
      }),
    );

    await handlers.display_list({});
    await handlers.display_active({});
    await handlers.display_config_get({});
    await handlers.display_config_get({ key: "desktop" });
    await handlers.display_config_set({ key: "desktop", patch: "width: 1280" });
    await handlers.display_select({ key: "desktop" });

    expect(displayList).toHaveBeenCalledOnce();
    expect(displayActive).toHaveBeenCalledOnce();
    expect(displayConfigGet).toHaveBeenNthCalledWith(1, "active");
    expect(displayConfigGet).toHaveBeenNthCalledWith(2, "desktop");
    expect(displayConfigSet).toHaveBeenCalledWith({
      key: "desktop",
      patch: "width: 1280",
    });
    expect(displaySelect).toHaveBeenCalledWith("desktop");
  });

  it("routes new editor and ops tools to their handlers", async () => {
    const health = vi.fn(async () => ({ ok: true }));
    const buildInfo = vi.fn(async () => ({
      gitCommit: "0123456789abcdef",
      gitCommitShort: "0123456789ab",
      gitDirty: false,
    }));
    const getSelection = vi.fn(async () => ({ selectedNodeId: 7 }));
    const getCameras = vi.fn(async () => ({ active: "editor_cam" }));
    const pick = vi.fn(async () => ({ hit: true }));
    const command = vi.fn(async () => ({ ok: true }));
    const waitFor = vi.fn(async () => ({ found: true }));
    const buildConfigure = vi.fn(async () => ({ exitCode: 0, label: "build.configure" }));
    const buildTarget = vi.fn(async () => ({ exitCode: 0, label: "build.target" }));
    const buildState = vi.fn(async () => ({ repoHeadShort: "0123456789ab" }));
    const start = vi.fn(async () => ({ running: true, pid: 321 }));
    const stop = vi.fn(async () => ({ running: false }));
    const restart = vi.fn(async () => ({ running: true, pid: 654 }));
    const logs = vi.fn(async () => ({ stdout: "out", stderr: "err" }));
    const handlers = createToolHandlers(
      makeInput({
        editorClient: {
          health,
          buildInfo,
          getSelection,
          getCameras,
          pick,
          command,
          waitFor,
        },
        editorOps: { start, stop, restart, logs },
        workspaceOps: { buildConfigure, buildTarget, buildState },
      }),
    );

    await handlers["editor.get_selection"]({});
    await handlers["editor.get_cameras"]({});
    await handlers["editor.get_build_info"]({});
    await handlers["editor.pick"]({ x: 11, y: 22 });
    await handlers["editor.command"]({ line: "scene list" });
    await handlers["editor.wait_for"]({ contains: "Scene" });
    await handlers["lxe_editor_get_selection"]({});
    await handlers["lxe_editor_get_cameras"]({});
    await handlers["lxe_editor_get_build_info"]({});
    await handlers["lxe_editor_pick"]({ x: 11, y: 22 });
    await handlers["lxe_editor_command"]({ line: "scene list" });
    await handlers["lxe_editor_wait_for"]({ contains: "Scene" });
    await handlers["lxe_editor_ensure_running"]({});
    await handlers["ops.build_configure"]({ buildDir: "/tmp/build" });
    await handlers["ops.build_target"]({ buildDir: "/tmp/build", target: "lxe_editor" });
    await handlers["ops.build_state"]({});
    await handlers["ops.editor_start"]({});
    await handlers["ops.editor_stop"]({});
    await handlers["ops.editor_restart"]({});
    await handlers["ops.editor_logs"]({});

    expect(getSelection).toHaveBeenCalledTimes(2);
    expect(getCameras).toHaveBeenCalledTimes(2);
    expect(buildInfo).toHaveBeenCalledTimes(2);
    expect(pick).toHaveBeenCalledWith(11, 22);
    expect(command).toHaveBeenCalledWith("scene list");
    expect(waitFor).toHaveBeenCalledWith({ contains: "Scene" });
    expect(health).toHaveBeenCalledOnce();
    expect(buildConfigure).toHaveBeenCalledWith("/tmp/build");
    expect(buildTarget).toHaveBeenCalledWith("/tmp/build", "lxe_editor");
    expect(buildState).toHaveBeenCalledOnce();
    expect(start).toHaveBeenCalledOnce();
    expect(stop).toHaveBeenCalledOnce();
    expect(restart).toHaveBeenCalledOnce();
    expect(logs).toHaveBeenCalledOnce();
  });

  it("returns a clear editor unavailable tool error without discovery", async () => {
    const handlers = createToolHandlers({
      ...makeInput(),
      editorClient: undefined,
    });

    await expect(handlers["lxe_editor_get_summary"]({})).resolves.toEqual({
      isError: true,
      content: [
        {
          type: "text",
          text: '{"ok":false,"error":{"code":"editor_unavailable","message":"lxe_editor HTTP API is unavailable: runtime_state.yaml and token file were not discovered"}}',
        },
      ],
    });
  });

  it("resolves the editor client dynamically for each tool call", async () => {
    const getSummary = vi.fn(async () => ({ sceneName: "Live" }));
    const provider = vi
      .fn()
      .mockReturnValueOnce(undefined)
      .mockReturnValueOnce({
        health: vi.fn(),
        getSummary,
        getSelection: vi.fn(),
        getCameras: vi.fn(),
        getToolbar: vi.fn(),
        getScene: vi.fn(),
        buildInfo: vi.fn(),
        pick: vi.fn(),
        command: vi.fn(),
        waitFor: vi.fn(),
        recordingStatus: vi.fn(),
        recordingEnable: vi.fn(),
        recordingDisable: vi.fn(),
        recordingStart: vi.fn(),
        recordingStop: vi.fn(),
        recordingList: vi.fn(),
        recordingRead: vi.fn(),
        recordingReplay: vi.fn(),
        recordingProbe: vi.fn(),
        displayList: vi.fn(),
        displayActive: vi.fn(),
        displayConfigGet: vi.fn(),
        displayConfigSet: vi.fn(),
        displaySelect: vi.fn(),
      });
    const handlers = createToolHandlers({
      ...makeInput(),
      editorClient: undefined,
      editorClientProvider: provider,
    });

    await expect(handlers["lxe_editor_get_summary"]({})).resolves.toMatchObject({
      isError: true,
    });
    await expect(handlers["lxe_editor_get_summary"]({})).resolves.toEqual({
      content: [{ type: "text", text: '{"sceneName":"Live"}' }],
    });
    expect(provider).toHaveBeenCalledTimes(2);
    expect(getSummary).toHaveBeenCalledOnce();
  });

  it("lists and reads lxe editor resources", async () => {
    const resources = createResourceHandlers(makeInput().editorClient);

    expect(resources.list()).toEqual([
      { uri: "lxe-editor://summary", name: "summary", mimeType: "application/json" },
      { uri: "lxe-editor://selection", name: "selection", mimeType: "application/json" },
      { uri: "lxe-editor://cameras", name: "cameras", mimeType: "application/json" },
      { uri: "lxe-editor://toolbar", name: "toolbar", mimeType: "application/json" },
      { uri: "lxe-editor://scene", name: "scene", mimeType: "application/json" },
    ]);
    await expect(resources.read("lxe-editor://toolbar")).resolves.toEqual({
      contents: [
        {
          uri: "lxe-editor://toolbar",
          mimeType: "application/json",
          text: '{"activeTool":"select"}',
        },
      ],
    });
  });

  it("resolves the editor client dynamically for each resource read", async () => {
    const getSummary = vi.fn(async () => ({ sceneName: "Live" }));
    const provider = vi
      .fn()
      .mockReturnValueOnce(undefined)
      .mockReturnValueOnce({
        health: vi.fn(),
        getSummary,
        getSelection: vi.fn(),
        getCameras: vi.fn(),
        getToolbar: vi.fn(),
        getScene: vi.fn(),
        buildInfo: vi.fn(),
        pick: vi.fn(),
        command: vi.fn(),
        waitFor: vi.fn(),
        recordingStatus: vi.fn(),
        recordingEnable: vi.fn(),
        recordingDisable: vi.fn(),
        recordingStart: vi.fn(),
        recordingStop: vi.fn(),
        recordingList: vi.fn(),
        recordingRead: vi.fn(),
        recordingReplay: vi.fn(),
        recordingProbe: vi.fn(),
        displayList: vi.fn(),
        displayActive: vi.fn(),
        displayConfigGet: vi.fn(),
        displayConfigSet: vi.fn(),
        displaySelect: vi.fn(),
      });
    const resources = createResourceHandlers(provider);

    await expect(resources.read("lxe-editor://summary")).rejects.toThrow(
      "lxe_editor HTTP API is unavailable",
    );
    await expect(resources.read("lxe-editor://summary")).resolves.toEqual({
      contents: [
        {
          uri: "lxe-editor://summary",
          mimeType: "application/json",
          text: '{"sceneName":"Live"}',
        },
      ],
    });
    expect(provider).toHaveBeenCalledTimes(2);
    expect(getSummary).toHaveBeenCalledOnce();
  });

  it("serves tools/list and tools/call over HTTP JSON-RPC at /mcp", async () => {
    const handlers = createToolHandlers(makeInput());
    const resources = createResourceHandlers(makeInput().editorClient);
    server = createMcpHttpServer({ handlers, resources });
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
          expect.objectContaining({ name: "editor.get_build_info" }),
          expect.objectContaining({ name: "lxe_editor_get_summary" }),
          expect.objectContaining({ name: "lxe_editor_get_build_info" }),
          expect.objectContaining({ name: "ops.editor_logs" }),
          expect.objectContaining({ name: "recording_status" }),
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

    const resourceListResponse = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 3,
        method: "resources/list",
      }),
    });

    await expect(resourceListResponse.json()).resolves.toEqual({
      jsonrpc: "2.0",
      id: 3,
      result: {
        resources: [
          { uri: "lxe-editor://summary", name: "summary", mimeType: "application/json" },
          { uri: "lxe-editor://selection", name: "selection", mimeType: "application/json" },
          { uri: "lxe-editor://cameras", name: "cameras", mimeType: "application/json" },
          { uri: "lxe-editor://toolbar", name: "toolbar", mimeType: "application/json" },
          { uri: "lxe-editor://scene", name: "scene", mimeType: "application/json" },
        ],
      },
    });

    const resourceReadResponse = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 4,
        method: "resources/read",
        params: { uri: "lxe-editor://summary" },
      }),
    });

    await expect(resourceReadResponse.json()).resolves.toEqual({
      jsonrpc: "2.0",
      id: 4,
      result: {
        contents: [
          {
            uri: "lxe-editor://summary",
            mimeType: "application/json",
            text: '{"sceneName":"Scene","dirty":false}',
          },
        ],
      },
    });
  });

  it("advertises tools and resources capabilities during initialize", async () => {
    const handlers = createToolHandlers(makeInput());
    const resources = createResourceHandlers(makeInput().editorClient);
    server = createMcpHttpServer({ handlers, resources });
    const port = await listen(server);
    const response = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 1,
        method: "initialize",
      }),
    });

    await expect(response.json()).resolves.toMatchObject({
      jsonrpc: "2.0",
      id: 1,
      result: {
        capabilities: {
          tools: {},
          resources: {},
        },
      },
    });
  });

  it("accepts common MCP client handshake methods", async () => {
    const handlers = createToolHandlers(makeInput());
    server = createMcpHttpServer({ handlers });
    const port = await listen(server);

    const initializedResponse = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        method: "notifications/initialized",
      }),
    });
    expect(initializedResponse.status).toBe(202);
    await expect(initializedResponse.text()).resolves.toBe("");

    const promptsResponse = await fetch(`http://127.0.0.1:${port}/mcp`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 2,
        method: "prompts/list",
      }),
    });
    await expect(promptsResponse.json()).resolves.toEqual({
      jsonrpc: "2.0",
      id: 2,
      result: { prompts: [] },
    });
  });
});
