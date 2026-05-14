import {
  createServer,
  type IncomingMessage,
  type Server,
  type ServerResponse,
} from "node:http";
import type { ToolArguments, ToolHandler, ToolResult } from "./types.js";

interface EditorClientSurface {
  health: () => Promise<unknown>;
  getSummary: () => Promise<unknown>;
  getSelection: () => Promise<unknown>;
  getCameras: () => Promise<unknown>;
  getToolbar: () => Promise<unknown>;
  getScene: () => Promise<unknown>;
  buildInfo: () => Promise<unknown>;
  pick: (x: number, y: number) => Promise<unknown>;
  command: (line: string) => Promise<unknown>;
  waitFor: (input: {
    contains?: string;
    resource?: string;
    timeoutMs?: number;
    intervalMs?: number;
  }) => Promise<unknown>;
  recordingStatus: () => Promise<unknown>;
  recordingEnable: () => Promise<unknown>;
  recordingDisable: (input: { force?: boolean }) => Promise<unknown>;
  recordingStart: (input: {
    detailLevel?: "basic" | "diagnostic" | "trace";
  }) => Promise<unknown>;
  recordingStop: (input: { save?: boolean }) => Promise<unknown>;
  recordingList: () => Promise<unknown>;
  recordingRead: (id: string) => Promise<unknown>;
  recordingReplay: (input: { id?: string; path?: string }) => Promise<unknown>;
  recordingProbe: (
    target: "summary" | "selection" | "cameras" | "toolbar" | "scene" | "state",
  ) => Promise<unknown>;
  displayList: () => Promise<unknown>;
  displayActive: () => Promise<unknown>;
  displayConfigGet: (key: string) => Promise<unknown>;
  displayConfigSet: (input: { key: string; patch: string }) => Promise<unknown>;
  displaySelect: (key: string) => Promise<unknown>;
}

type EditorClientProvider = () =>
  | EditorClientSurface
  | undefined
  | Promise<EditorClientSurface | undefined>;

interface EditorOpsSurface {
  start: () => Promise<unknown>;
  stop: () => Promise<unknown>;
  restart: () => Promise<unknown>;
  status: () => Promise<unknown>;
  logs: () => Promise<unknown>;
}

interface WorkspaceOpsSurface {
  repoPull: () => Promise<unknown>;
  buildConfigure: (buildDir?: string) => Promise<unknown>;
  buildTarget: (buildDir?: string, target?: string) => Promise<unknown>;
  buildState: () => Promise<unknown> | unknown;
}

interface ManagerOpsSurface {
  restart: () => Promise<unknown>;
}

interface DashboardConfig {
  startedAt: string;
  host: string;
  port: number;
}

const POST_RESPONSE_ACTION = Symbol("postResponseAction");

interface PostResponseToolResult extends ToolResult {
  [POST_RESPONSE_ACTION]?: () => void;
}

interface MaybeManagerRestartResult {
  scheduleRestart?: unknown;
}

export function createToolHandlers(input: {
  editorOps: EditorOpsSurface;
  editorClient?: EditorClientSurface;
  editorClientProvider?: EditorClientProvider;
  managerOps?: ManagerOpsSurface;
  workspaceOps: WorkspaceOpsSurface;
}): Record<string, ToolHandler> {
  const editorUnavailable = () =>
    errorText(
      "editor_unavailable",
      "lxe_editor HTTP API is unavailable: runtime_state.yaml and token file were not discovered",
    );
  const getEditorClient = input.editorClientProvider ?? (() => input.editorClient);
  const withEditorClient = async (
    fn: (editorClient: EditorClientSurface) => Promise<unknown>,
  ): Promise<ToolResult> => {
    const editorClient = await getEditorClient();
    return editorClient ? jsonText(await fn(editorClient)) : editorUnavailable();
  };

  const handlers: Record<string, ToolHandler> = {
    "editor.get_summary": async () =>
      withEditorClient((editorClient) => editorClient.getSummary()),
    "editor.get_selection": async () =>
      withEditorClient((editorClient) => editorClient.getSelection()),
    "editor.get_cameras": async () =>
      withEditorClient((editorClient) => editorClient.getCameras()),
    "editor.get_build_info": async () =>
      withEditorClient((editorClient) => editorClient.buildInfo()),
    "editor.pick": async (args) =>
      withEditorClient((editorClient) =>
        editorClient.pick(readNumber(args, "x"), readNumber(args, "y")),
      ),
    "editor.command": async (args) =>
      withEditorClient((editorClient) => editorClient.command(readCommandLine(args))),
    "editor.wait_for": async (args) =>
      withEditorClient((editorClient) =>
        editorClient.waitFor({
          contains: optionalString(args, "contains"),
          resource: optionalString(args, "resource"),
          timeoutMs: optionalNumber(args, "timeoutMs"),
          intervalMs: optionalNumber(args, "intervalMs"),
        }),
      ),
    recording_status: async () =>
      withEditorClient((editorClient) => editorClient.recordingStatus()),
    recording_enable: async () =>
      withEditorClient((editorClient) => editorClient.recordingEnable()),
    recording_disable: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingDisable({ force: optionalBoolean(args, "force") }),
      ),
    recording_start: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingStart({
          detailLevel: optionalDetailLevel(args, "detailLevel"),
        }),
      ),
    recording_stop: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingStop({ save: optionalBoolean(args, "save") }),
      ),
    recording_list: async () =>
      withEditorClient((editorClient) => editorClient.recordingList()),
    recording_read: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingRead(readString(args, "id")),
      ),
    recording_replay: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingReplay({
          id: optionalString(args, "id"),
          path: optionalString(args, "path"),
        }),
      ),
    recording_probe: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.recordingProbe(readProbeTarget(args, "target")),
      ),
    display_list: async () =>
      withEditorClient((editorClient) => editorClient.displayList()),
    display_active: async () =>
      withEditorClient((editorClient) => editorClient.displayActive()),
    display_config_get: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.displayConfigGet(optionalString(args, "key") ?? "active"),
      ),
    display_config_set: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.displayConfigSet({
          key: readString(args, "key"),
          patch: readString(args, "patch"),
        }),
      ),
    display_select: async (args) =>
      withEditorClient((editorClient) =>
        editorClient.displaySelect(readString(args, "key")),
      ),
    "ops.repo_pull": async () => guarded("ops.repo_pull", () => input.workspaceOps.repoPull()),
    "ops.build_configure": async (args) =>
      guarded("ops.build_configure", () =>
        input.workspaceOps.buildConfigure(optionalString(args, "buildDir")),
      ),
    "ops.build_target": async (args) =>
      guarded("ops.build_target", () =>
        input.workspaceOps.buildTarget(
          optionalString(args, "buildDir"),
          optionalString(args, "target"),
        ),
      ),
    "ops.build_state": async () => jsonText(await input.workspaceOps.buildState()),
    "ops.editor_start": async () => jsonText(await input.editorOps.start()),
    "ops.editor_stop": async () => jsonText(await input.editorOps.stop()),
    "ops.editor_restart": async () => jsonText(await input.editorOps.restart()),
    "ops.editor_status": async () => jsonText(await input.editorOps.status()),
    "ops.editor_logs": async () => jsonText(await input.editorOps.logs()),
    "ops.manager_restart": async () => {
      if (!input.managerOps) {
        return errorText(
            "manager_restart_unavailable",
            "manager restart is unavailable in this manager process",
        );
      }

      const result = await input.managerOps.restart();
      const scheduleRestart = readScheduleRestart(result);
      return scheduleRestart
        ? jsonTextWithPostResponseAction(result, scheduleRestart)
        : jsonText(result);
    },
  };

  handlers.lxe_editor_get_summary = handlers["editor.get_summary"];
  handlers.lxe_editor_get_selection = handlers["editor.get_selection"];
  handlers.lxe_editor_get_cameras = handlers["editor.get_cameras"];
  handlers.lxe_editor_get_build_info = handlers["editor.get_build_info"];
  handlers.lxe_editor_pick = handlers["editor.pick"];
  handlers.lxe_editor_command = handlers["editor.command"];
  handlers.lxe_editor_wait_for = handlers["editor.wait_for"];
  handlers.lxe_editor_ensure_running = async () =>
    withEditorClient((editorClient) => editorClient.health());

  return handlers;
}

export interface ResourceDescriptor {
  uri: string;
  name: string;
  mimeType: string;
}

export interface ResourceReadResult {
  contents: Array<{
    uri: string;
    mimeType: string;
    text: string;
  }>;
}

interface ResourceHandlers {
  list: () => ResourceDescriptor[];
  read: (uri: string) => Promise<ResourceReadResult>;
}

const EDITOR_RESOURCE_NAMES = [
  "summary",
  "selection",
  "cameras",
  "toolbar",
  "scene",
] as const;

export function createResourceHandlers(
  editorClientOrProvider?: EditorClientSurface | EditorClientProvider,
): ResourceHandlers {
  const getEditorClient =
    typeof editorClientOrProvider === "function"
      ? editorClientOrProvider
      : () => editorClientOrProvider;

  return {
    list: () =>
      EDITOR_RESOURCE_NAMES.map((name) => ({
        uri: `lxe-editor://${name}`,
        name,
        mimeType: "application/json",
      })),
    read: async (uri) => {
      const normalized = normalizeEditorResourceUri(uri);
      const editorClient = await getEditorClient();
      if (!editorClient) {
        throw new Error(
          "lxe_editor HTTP API is unavailable: runtime_state.yaml and token file were not discovered",
        );
      }
      const value = await readEditorResource(editorClient, normalized);
      return {
        contents: [
          {
            uri: `lxe-editor://${normalized}`,
            mimeType: "application/json",
            text: JSON.stringify(value),
          },
        ],
      };
    },
  };
}

export function createMcpHttpServer(input: {
  handlers: Record<string, ToolHandler>;
  resources?: ResourceHandlers;
  bearerToken?: string;
  dashboard?: DashboardConfig;
}): Server {
  return createServer(async (request, response) => {
    if (await handleDashboardRequest(input, request, response)) {
      return;
    }

    if (request.url !== "/mcp") {
      writeJson(response, 404, {
        ok: false,
        error: { code: "not_found", message: "unknown endpoint" },
      });
      return;
    }
    if (request.method !== "POST") {
      writeJson(response, 405, {
        ok: false,
        error: { code: "method_not_allowed", message: "POST required" },
      });
      return;
    }
    if (input.bearerToken && !isAuthorized(request, input.bearerToken)) {
      writeJson(response, 401, {
        ok: false,
        error: { code: "unauthorized", message: "missing or invalid token" },
      });
      return;
    }

    try {
      const rpcRequest = JSON.parse(await readBody(request)) as JsonRpcRequest;
      const rpcResponse = await handleJsonRpcRequest(
        input.handlers,
        rpcRequest,
        input.resources,
      );
      if (!rpcResponse) {
        response.writeHead(202);
        response.end();
        return;
      }
      writeJson(response, 200, rpcResponse, postResponseActionFor(rpcResponse));
    } catch (error) {
      writeJson(response, 200, {
        jsonrpc: "2.0",
        id: null,
        error: {
          code: -32700,
          message: error instanceof Error ? error.message : String(error),
        },
      });
    }
  });
}

async function handleDashboardRequest(
  input: {
    handlers: Record<string, ToolHandler>;
    bearerToken?: string;
    dashboard?: DashboardConfig;
  },
  request: IncomingMessage,
  response: ServerResponse,
): Promise<boolean> {
  const url = new URL(request.url ?? "/", "http://127.0.0.1");
  if (url.pathname === "/" || url.pathname === "/dashboard") {
    if (request.method !== "GET") {
      writeJson(response, 405, {
        ok: false,
        error: { code: "method_not_allowed", message: "GET required" },
      });
      return true;
    }
    writeHtml(response, dashboardHtml());
    return true;
  }

  if (url.pathname === "/api/manager/status") {
    if (request.method !== "GET") {
      writeJson(response, 405, {
        ok: false,
        error: { code: "method_not_allowed", message: "GET required" },
      });
      return true;
    }
    if (input.bearerToken && !isAuthorized(request, input.bearerToken)) {
      writeUnauthorized(response);
      return true;
    }
    writeJson(response, 200, await dashboardStatus(input));
    return true;
  }

  if (url.pathname === "/api/manager/tool-call") {
    if (request.method !== "POST") {
      writeJson(response, 405, {
        ok: false,
        error: { code: "method_not_allowed", message: "POST required" },
      });
      return true;
    }
    if (input.bearerToken && !isAuthorized(request, input.bearerToken)) {
      writeUnauthorized(response);
      return true;
    }
    const body = JSON.parse(await readBody(request)) as {
      name?: unknown;
      arguments?: unknown;
    };
    if (typeof body.name !== "string" || !DASHBOARD_ALLOWED_TOOLS.has(body.name)) {
      writeJson(response, 400, {
        ok: false,
        error: {
          code: "tool_not_allowed",
          message: `dashboard cannot call tool: ${String(body.name)}`,
        },
      });
      return true;
    }
    const handler = input.handlers[body.name];
    if (!handler) {
      writeJson(response, 400, {
        ok: false,
        error: {
          code: "unknown_tool",
          message: `unknown tool: ${body.name}`,
        },
      });
      return true;
    }
    const args = isObject(body.arguments) ? body.arguments : {};
    writeJson(response, 200, await handler(args));
    return true;
  }

  return false;
}

async function dashboardStatus(input: {
  handlers: Record<string, ToolHandler>;
  dashboard?: DashboardConfig;
}): Promise<unknown> {
  const startedAt = input.dashboard?.startedAt ?? new Date(0).toISOString();
  const host = input.dashboard?.host ?? "";
  const port = input.dashboard?.port ?? 0;
  return {
    manager: {
      startedAt,
      host,
      port,
      toolCount: Object.keys(input.handlers).length,
    },
    editor: await toolJson(input.handlers, "ops.editor_status", {}),
    build: await toolJson(input.handlers, "ops.build_state", {}),
    logs: await toolJson(input.handlers, "ops.editor_logs", {}),
    tools: Object.keys(input.handlers).sort(),
  };
}

async function toolJson(
  handlers: Record<string, ToolHandler>,
  name: string,
  args: ToolArguments,
): Promise<unknown> {
  const handler = handlers[name];
  if (!handler) {
    return undefined;
  }
  const result = await handler(args);
  const text = result.content[0]?.text;
  return typeof text === "string" ? JSON.parse(text) : undefined;
}

const DASHBOARD_ALLOWED_TOOLS = new Set([
  "ops.editor_start",
  "ops.editor_stop",
  "ops.editor_restart",
  "ops.editor_status",
  "ops.editor_logs",
  "ops.repo_pull",
  "ops.build_target",
  "ops.build_state",
  "ops.manager_restart",
]);

export async function handleJsonRpcRequest(
  handlers: Record<string, ToolHandler>,
  request: JsonRpcRequest,
  resources?: ResourceHandlers,
): Promise<JsonRpcResponse | undefined> {
  const isNotification = request.id === undefined;
  const id: JsonRpcId = isNotification ? null : (request.id ?? null);
  if (request.jsonrpc !== "2.0" || typeof request.method !== "string") {
    return rpcError(id, -32600, "invalid JSON-RPC request");
  }

  switch (request.method) {
    case "initialize":
      return rpcResult(id, {
        protocolVersion: "2024-11-05",
        capabilities: { tools: {}, resources: {} },
        serverInfo: { name: "lxe_manager", version: "0.1.0" },
      });
    case "ping":
      return rpcResult(id, {});
    case "notifications/initialized":
      return isNotification ? undefined : rpcResult(id, {});
    case "prompts/list":
      return rpcResult(id, { prompts: [] });
    case "tools/list":
      return rpcResult(id, {
        tools: Object.keys(handlers)
          .sort()
          .map((name) => ({
            name,
            description: name,
            inputSchema: { type: "object", additionalProperties: true },
          })),
      });
    case "tools/call":
      return callTool(handlers, id, request.params);
    case "resources/list":
      return rpcResult(id, { resources: resources?.list() ?? [] });
    case "resources/read":
      return readResource(resources, id, request.params);
    default:
      return rpcError(id, -32601, `unknown method: ${request.method}`);
  }
}

async function readResource(
  resources: ResourceHandlers | undefined,
  id: JsonRpcId,
  params: unknown,
): Promise<JsonRpcResponse> {
  if (!resources) {
    return rpcError(id, -32601, "resources are not available");
  }
  if (!isObject(params) || typeof params.uri !== "string") {
    return rpcError(id, -32602, "resources/read requires params.uri");
  }

  try {
    return rpcResult(id, await resources.read(params.uri));
  } catch (error) {
    return rpcError(
      id,
      -32000,
      error instanceof Error ? error.message : String(error),
    );
  }
}

async function callTool(
  handlers: Record<string, ToolHandler>,
  id: JsonRpcId,
  params: unknown,
): Promise<JsonRpcResponse> {
  if (!isObject(params) || typeof params.name !== "string") {
    return rpcError(id, -32602, "tools/call requires params.name");
  }

  const handler = handlers[params.name];
  if (!handler) {
    return rpcError(id, -32602, `unknown tool: ${params.name}`);
  }

  try {
    const args = isObject(params.arguments) ? params.arguments : {};
    return rpcResult(id, await handler(args));
  } catch (error) {
    return rpcError(
      id,
      -32000,
      error instanceof Error ? error.message : String(error),
    );
  }
}

async function guarded(label: string, fn: () => Promise<unknown>): Promise<ToolResult> {
  try {
    return jsonText(await fn());
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`${label} failed: ${message}`);
  }
}

function jsonText(value: unknown): ToolResult {
  return {
    content: [{ type: "text", text: JSON.stringify(value) }],
  };
}

function jsonTextWithPostResponseAction(
  value: unknown,
  action: () => void,
): ToolResult {
  const result = jsonText(value) as PostResponseToolResult;
  Object.defineProperty(result, POST_RESPONSE_ACTION, {
    value: action,
  });
  return result;
}

function readScheduleRestart(value: unknown): (() => void) | undefined {
  if (!isObject(value)) {
    return undefined;
  }
  const candidate = (value as MaybeManagerRestartResult).scheduleRestart;
  return typeof candidate === "function" ? (candidate as () => void) : undefined;
}

function postResponseActionFor(response: JsonRpcResponse): (() => void) | undefined {
  const result = response.result;
  if (!isObject(result)) {
    return undefined;
  }
  return (result as unknown as PostResponseToolResult)[POST_RESPONSE_ACTION];
}

function errorText(code: string, message: string): ToolResult {
  return {
    isError: true,
    content: [
      {
        type: "text",
        text: JSON.stringify({ ok: false, error: { code, message } }),
      },
    ],
  };
}

function readString(args: ToolArguments, key: string): string {
  const value = args[key];
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`missing string argument: ${key}`);
  }
  return value;
}

function readCommandLine(args: ToolArguments): string {
  const line = args.line;
  if (typeof line === "string" && line.length > 0) {
    return line;
  }

  const command = args.command;
  if (typeof command === "string" && command.length > 0) {
    return command;
  }

  throw new Error("missing string argument: line or command");
}

function optionalString(args: ToolArguments, key: string): string | undefined {
  const value = args[key];
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "string") {
    throw new Error(`invalid string argument: ${key}`);
  }
  return value;
}

function readNumber(args: ToolArguments, key: string): number {
  const value = args[key];
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`missing number argument: ${key}`);
  }
  return value;
}

function optionalNumber(args: ToolArguments, key: string): number | undefined {
  const value = args[key];
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`invalid number argument: ${key}`);
  }
  return value;
}

function optionalBoolean(args: ToolArguments, key: string): boolean | undefined {
  const value = args[key];
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "boolean") {
    throw new Error(`invalid boolean argument: ${key}`);
  }
  return value;
}

function optionalDetailLevel(
  args: ToolArguments,
  key: string,
): "basic" | "diagnostic" | "trace" | undefined {
  const value = optionalString(args, key);
  if (value === undefined || value === "basic" || value === "diagnostic" || value === "trace") {
    return value;
  }
  throw new Error(`invalid recording detail level: ${key}`);
}

function readProbeTarget(
  args: ToolArguments,
  key: string,
): "summary" | "selection" | "cameras" | "toolbar" | "scene" | "state" {
  const value = readString(args, key);
  if (
    value === "summary" ||
    value === "selection" ||
    value === "cameras" ||
    value === "toolbar" ||
    value === "scene" ||
    value === "state"
  ) {
    return value;
  }
  throw new Error(`invalid recording probe target: ${key}`);
}

function normalizeEditorResourceUri(uri: string): (typeof EDITOR_RESOURCE_NAMES)[number] {
  const resourceName = uri.replace(/^lxe-editor:\/\//, "");
  if (EDITOR_RESOURCE_NAMES.some((name) => name === resourceName)) {
    return resourceName as (typeof EDITOR_RESOURCE_NAMES)[number];
  }
  throw new Error(`unknown resource: ${uri}`);
}

async function readEditorResource(
  editorClient: EditorClientSurface,
  resourceName: (typeof EDITOR_RESOURCE_NAMES)[number],
): Promise<unknown> {
  switch (resourceName) {
    case "summary":
      return editorClient.getSummary();
    case "selection":
      return editorClient.getSelection();
    case "cameras":
      return editorClient.getCameras();
    case "toolbar":
      return editorClient.getToolbar();
    case "scene":
      return editorClient.getScene();
  }
}

function isAuthorized(request: IncomingMessage, bearerToken: string): boolean {
  return request.headers.authorization === `Bearer ${bearerToken}`;
}

function writeUnauthorized(response: ServerResponse): void {
  writeJson(response, 401, {
    ok: false,
    error: { code: "unauthorized", message: "missing or invalid token" },
  });
}

function readBody(request: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    request.on("data", (chunk: Buffer) => chunks.push(chunk));
    request.on("error", reject);
    request.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
  });
}

function writeJson(
  response: ServerResponse,
  statusCode: number,
  value: unknown,
  callback?: () => void,
): void {
  response.writeHead(statusCode, { "content-type": "application/json" });
  response.end(JSON.stringify(value), callback);
}

function writeHtml(response: ServerResponse, html: string): void {
  response.writeHead(200, { "content-type": "text/html; charset=utf-8" });
  response.end(html);
}

function dashboardHtml(): string {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>lxe_manager</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; background: #16181d; color: #e7e9ef; }
    body { margin: 0; }
    header { padding: 18px 24px; border-bottom: 1px solid #30343d; background: #1e222a; }
    main { display: grid; grid-template-columns: 320px 1fr; gap: 18px; padding: 18px; }
    section { border: 1px solid #30343d; border-radius: 6px; background: #20242c; padding: 14px; }
    h1 { margin: 0; font-size: 20px; }
    h2 { margin: 0 0 10px; font-size: 15px; }
    label { display: block; margin-bottom: 10px; color: #aab0bd; font-size: 12px; }
    input, select, textarea, button { box-sizing: border-box; width: 100%; border-radius: 5px; border: 1px solid #3b414d; background: #14171c; color: #f3f5f8; padding: 8px; }
    button { cursor: pointer; background: #2f6fed; border-color: #2f6fed; font-weight: 600; }
    pre { min-height: 260px; overflow: auto; white-space: pre-wrap; background: #111318; border-radius: 5px; padding: 12px; }
    .grid { display: grid; gap: 10px; }
    .row { display: flex; gap: 8px; }
    .row button { flex: 1; }
    @media (max-width: 800px) { main { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <header><h1>lxe_manager</h1></header>
  <main>
    <section class="grid">
      <h2>Connection</h2>
      <label>Bearer token<input id="token" type="password" autocomplete="off"></label>
      <div class="row"><button id="saveToken">Save</button><button id="refresh">Refresh</button></div>
      <h2>Actions</h2>
      <select id="tool">
        <option>ops.editor_start</option>
        <option>ops.editor_stop</option>
        <option>ops.editor_restart</option>
        <option>ops.editor_status</option>
        <option>ops.editor_logs</option>
        <option>ops.repo_pull</option>
        <option>ops.build_target</option>
        <option>ops.build_state</option>
        <option>ops.manager_restart</option>
      </select>
      <textarea id="args" rows="5">{}</textarea>
      <button id="call">Call Tool</button>
    </section>
    <section>
      <h2>Status / Result</h2>
      <pre id="output"></pre>
    </section>
  </main>
  <script>
    const tokenInput = document.getElementById('token');
    const output = document.getElementById('output');
    tokenInput.value = localStorage.getItem('lxe_manager_token') || '';
    function headers() {
      const token = tokenInput.value.trim();
      return token ? { authorization: 'Bearer ' + token } : {};
    }
    async function refresh() {
      const response = await fetch('/api/manager/status', { headers: headers() });
      output.textContent = JSON.stringify(await response.json(), null, 2);
    }
    document.getElementById('saveToken').onclick = () => {
      localStorage.setItem('lxe_manager_token', tokenInput.value.trim());
      refresh();
    };
    document.getElementById('refresh').onclick = refresh;
    document.getElementById('call').onclick = async () => {
      const response = await fetch('/api/manager/tool-call', {
        method: 'POST',
        headers: { ...headers(), 'content-type': 'application/json' },
        body: JSON.stringify({
          name: document.getElementById('tool').value,
          arguments: JSON.parse(document.getElementById('args').value || '{}'),
        }),
      });
      output.textContent = JSON.stringify(await response.json(), null, 2);
    };
    refresh().catch(error => { output.textContent = String(error); });
  </script>
</body>
</html>`;
}

function rpcResult(id: JsonRpcId, result: unknown): JsonRpcResponse {
  return { jsonrpc: "2.0", id, result };
}

function rpcError(
  id: JsonRpcId,
  code: number,
  message: string,
): JsonRpcResponse {
  return {
    jsonrpc: "2.0",
    id,
    error: { code, message },
  };
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

type JsonRpcId = string | number | null;

interface JsonRpcRequest {
  jsonrpc?: string;
  id?: JsonRpcId;
  method?: string;
  params?: unknown;
}

interface JsonRpcResponse {
  jsonrpc: "2.0";
  id: JsonRpcId;
  result?: unknown;
  error?: { code: number; message: string };
}
