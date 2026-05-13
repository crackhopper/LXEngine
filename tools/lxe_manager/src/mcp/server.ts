import {
  createServer,
  type IncomingMessage,
  type Server,
  type ServerResponse,
} from "node:http";
import type { ToolArguments, ToolHandler, ToolResult } from "./types.js";

interface EditorClientSurface {
  getSummary: () => Promise<unknown>;
  getSelection: () => Promise<unknown>;
  getCameras: () => Promise<unknown>;
  pick: (x: number, y: number) => Promise<unknown>;
  command: (line: string) => Promise<unknown>;
  waitFor: (input: {
    contains?: string;
    resource?: string;
    timeoutMs?: number;
    intervalMs?: number;
  }) => Promise<unknown>;
}

interface EditorOpsSurface {
  start: () => Promise<unknown>;
  stop: () => Promise<unknown>;
  status: () => Promise<unknown>;
  logs: () => Promise<unknown>;
}

interface WorkspaceOpsSurface {
  repoPull: () => Promise<unknown>;
  buildConfigure: (buildDir?: string) => Promise<unknown>;
  buildTarget: (buildDir?: string, target?: string) => Promise<unknown>;
}

export function createToolHandlers(input: {
  editorOps: EditorOpsSurface;
  editorClient?: EditorClientSurface;
  workspaceOps: WorkspaceOpsSurface;
}): Record<string, ToolHandler> {
  const editorUnavailable = () =>
    errorText(
      "editor_unavailable",
      "lxe_editor HTTP API is unavailable: runtime_state.yaml and token file were not discovered",
    );
  const editorClient = input.editorClient;

  return {
    "editor.get_summary": async () =>
      editorClient ? jsonText(await editorClient.getSummary()) : editorUnavailable(),
    "editor.get_selection": async () =>
      editorClient ? jsonText(await editorClient.getSelection()) : editorUnavailable(),
    "editor.get_cameras": async () =>
      editorClient ? jsonText(await editorClient.getCameras()) : editorUnavailable(),
    "editor.pick": async (args) =>
      editorClient
        ? jsonText(await editorClient.pick(readNumber(args, "x"), readNumber(args, "y")))
        : editorUnavailable(),
    "editor.command": async (args) =>
      editorClient
        ? jsonText(await editorClient.command(readString(args, "line")))
        : editorUnavailable(),
    "editor.wait_for": async (args) =>
      editorClient
        ? jsonText(
            await editorClient.waitFor({
              contains: optionalString(args, "contains"),
              resource: optionalString(args, "resource"),
              timeoutMs: optionalNumber(args, "timeoutMs"),
              intervalMs: optionalNumber(args, "intervalMs"),
            }),
          )
        : editorUnavailable(),
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
    "ops.editor_start": async () => jsonText(await input.editorOps.start()),
    "ops.editor_stop": async () => jsonText(await input.editorOps.stop()),
    "ops.editor_status": async () => jsonText(await input.editorOps.status()),
    "ops.editor_logs": async () => jsonText(await input.editorOps.logs()),
  };
}

export function createMcpHttpServer(input: {
  handlers: Record<string, ToolHandler>;
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
      writeJson(response, 200, await handleJsonRpcRequest(input.handlers, rpcRequest));
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
): Promise<JsonRpcResponse> {
  const id = request.id ?? null;
  if (request.jsonrpc !== "2.0" || typeof request.method !== "string") {
    return rpcError(id, -32600, "invalid JSON-RPC request");
  }

  switch (request.method) {
    case "initialize":
      return rpcResult(id, {
        protocolVersion: "2024-11-05",
        capabilities: { tools: {} },
        serverInfo: { name: "lxe_manager", version: "0.1.0" },
      });
    case "ping":
      return rpcResult(id, {});
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
    default:
      return rpcError(id, -32601, `unknown method: ${request.method}`);
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
): void {
  response.writeHead(statusCode, { "content-type": "application/json" });
  response.end(JSON.stringify(value));
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
