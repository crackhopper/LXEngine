export interface EditorRuntimeState {
  pid: number;
  httpHost: string;
  httpPort: number;
  wsHost: string;
  wsPort: number;
  tokenFile: string;
  startedAt: string;
}

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
    pid: Number(values.get("pid") ?? "0"),
    httpHost: values.get("httpHost") ?? "",
    httpPort: Number(values.get("httpPort") ?? "0"),
    wsHost: values.get("wsHost") ?? "",
    wsPort: Number(values.get("wsPort") ?? "0"),
    tokenFile: values.get("tokenFile") ?? "",
    startedAt: values.get("startedAt") ?? "",
  };
}

export async function runtimeStateIsReachable(
  state: EditorRuntimeState,
): Promise<boolean> {
  try {
    const response = await fetch(
      `http://${state.httpHost}:${state.httpPort}/health`,
    );
    return response.ok;
  } catch {
    return false;
  }
}
