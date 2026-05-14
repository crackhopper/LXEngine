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
      withEditorClient((editorClient) => editorClient.command(readString(args, "line"))),
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
}): Server {
  return createServer(async (request, response) => {
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
