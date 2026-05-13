import { randomBytes } from "node:crypto";

export interface ManagerCliOptions {
  host: string;
  port: number;
  repoRoot?: string;
  runtimeRoot?: string;
  editorExecutable?: string;
  bearerToken: string;
  bearerTokenGenerated: boolean;
}

export function parseManagerCliOptions(
  argv: string[],
  env: NodeJS.ProcessEnv = process.env,
  generateToken: () => string = generateBearerToken,
): ManagerCliOptions {
  const options: ManagerCliOptions = {
    host: env.LXE_MANAGER_HOST ?? "127.0.0.1",
    port: readPort(env.LXE_MANAGER_PORT, 3880, "LXE_MANAGER_PORT"),
    repoRoot: env.LXE_MANAGER_REPO_ROOT,
    runtimeRoot: env.LXE_MANAGER_RUNTIME_ROOT,
    editorExecutable: env.LXE_MANAGER_EDITOR_EXECUTABLE,
    bearerToken: env.LXE_MANAGER_MCP_BEARER_TOKEN ?? "",
    bearerTokenGenerated: false,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (!arg.startsWith("--")) {
      readPositionalHostPort(options, argv.slice(index));
      break;
    }
    switch (arg) {
      case "--host":
      case "--mcp-host":
        options.host = readValue(argv, ++index, arg);
        break;
      case "--port":
      case "--mcp-port":
        options.port = readPort(readValue(argv, ++index, arg), undefined, arg);
        break;
      case "--repo-root":
        options.repoRoot = readValue(argv, ++index, arg);
        break;
      case "--runtime-root":
        options.runtimeRoot = readValue(argv, ++index, arg);
        break;
      case "--editor-executable":
        options.editorExecutable = readValue(argv, ++index, arg);
        break;
      case "--token":
      case "--bearer-token":
        options.bearerToken = readValue(argv, ++index, arg);
        options.bearerTokenGenerated = false;
        break;
      default:
        throw new Error(`unknown lxe_manager argument: ${arg}`);
    }
  }

  if (!options.bearerToken) {
    options.bearerToken = generateToken();
    options.bearerTokenGenerated = true;
  }
  validateManagerCliOptions(options);
  return options;
}

function readPositionalHostPort(options: ManagerCliOptions, values: string[]): void {
  if (values.length > 2) {
    throw new Error(`unexpected lxe_manager positional arguments: ${values.join(" ")}`);
  }
  options.host = values[0];
  if (values[1] !== undefined) {
    options.port = readPort(values[1], undefined, "positional port");
  }
}

function validateManagerCliOptions(options: ManagerCliOptions): void {
  if (!options.host.trim()) {
    throw new Error("lxe_manager host must not be empty");
  }
}

function readValue(argv: string[], index: number, flag: string): string {
  const value = argv[index];
  if (!value || value.startsWith("--")) {
    throw new Error(`${flag} requires a value`);
  }
  return value;
}

function readPort(
  value: string | undefined,
  fallback: number | undefined,
  label: string,
): number {
  if (value === undefined) {
    if (fallback === undefined) {
      throw new Error(`${label} requires a value`);
    }
    return fallback;
  }
  const port = Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65_535) {
    throw new Error(`invalid ${label}: ${value}`);
  }
  return port;
}

function generateBearerToken(): string {
  return randomBytes(32).toString("hex");
}
