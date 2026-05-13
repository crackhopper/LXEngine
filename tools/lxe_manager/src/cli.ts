export interface ManagerCliOptions {
  host: string;
  port: number;
  repoRoot?: string;
  runtimeRoot?: string;
  editorExecutable?: string;
  bearerToken?: string;
}

export function parseManagerCliOptions(
  argv: string[],
  env: NodeJS.ProcessEnv = process.env,
): ManagerCliOptions {
  const options: ManagerCliOptions = {
    host: env.LXE_MANAGER_HOST ?? "127.0.0.1",
    port: readPort(env.LXE_MANAGER_PORT, 3880, "LXE_MANAGER_PORT"),
    repoRoot: env.LXE_MANAGER_REPO_ROOT,
    runtimeRoot: env.LXE_MANAGER_RUNTIME_ROOT,
    editorExecutable: env.LXE_MANAGER_EDITOR_EXECUTABLE,
    bearerToken: env.LXE_MANAGER_MCP_BEARER_TOKEN,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
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
        break;
      default:
        throw new Error(`unknown lxe_manager argument: ${arg}`);
    }
  }

  validateManagerCliOptions(options);
  return options;
}

function validateManagerCliOptions(options: ManagerCliOptions): void {
  if (!options.host.trim()) {
    throw new Error("lxe_manager host must not be empty");
  }
  if (!isLoopbackHost(options.host) && !options.bearerToken) {
    throw new Error(
      "LXE_MANAGER_MCP_BEARER_TOKEN or --token is required when binding lxe_manager to a non-loopback host",
    );
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

function isLoopbackHost(host: string): boolean {
  const normalized = host.trim().toLowerCase();
  return (
    normalized === "localhost" ||
    normalized === "127.0.0.1" ||
    normalized === "::1" ||
    normalized === "[::1]"
  );
}
