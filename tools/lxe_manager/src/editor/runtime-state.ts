export interface EditorRuntimeState {
  pid: number;
  httpHost: string;
  httpPort: number;
  wsHost: string;
  wsPort: number;
  tokenFile: string;
  startedAt: string;
}

const DEFAULT_HEALTH_TIMEOUT_MS = 1_000;

export function loadRuntimeState(text: string): EditorRuntimeState {
  const values = new Map<string, string>();

  for (const rawLine of text.split("\n")) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#") || !line.includes(":")) {
      continue;
    }

    const separatorIndex = line.indexOf(":");
    const key = line.slice(0, separatorIndex).trim();
    const value = line.slice(separatorIndex + 1).trim();
    values.set(key, value);
  }

  return {
    pid: readInteger(values, "pid", 1, Number.MAX_SAFE_INTEGER),
    httpHost: readString(values, "httpHost"),
    httpPort: readInteger(values, "httpPort", 1, 65_535),
    wsHost: readString(values, "wsHost"),
    wsPort: readInteger(values, "wsPort", 1, 65_535),
    tokenFile: readString(values, "tokenFile"),
    startedAt: readString(values, "startedAt"),
  };
}

export async function runtimeStateIsReachable(
  state: EditorRuntimeState,
  timeoutMs = DEFAULT_HEALTH_TIMEOUT_MS,
): Promise<boolean> {
  try {
    const response = await fetch(
      `http://${state.httpHost}:${state.httpPort}/health`,
      { signal: AbortSignal.timeout(timeoutMs) },
    );
    return response.ok;
  } catch {
    return false;
  }
}

function readString(values: Map<string, string>, key: string): string {
  const value = values.get(key)?.trim();
  if (!value) {
    throw new Error(`runtime state missing ${key}`);
  }
  return value;
}

function readInteger(
  values: Map<string, string>,
  key: string,
  min: number,
  max: number,
): number {
  const value = Number(values.get(key));
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`runtime state invalid ${key}`);
  }
  return value;
}
