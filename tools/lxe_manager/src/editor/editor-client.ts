export class EditorClient {
  constructor(
    private readonly input: {
      httpBaseUrl: string;
      bearerToken?: string;
      timeoutMs?: number;
    },
  ) {}

  async health(): Promise<unknown> {
    return this.requestJson("GET", "/health");
  }

  async getSummary(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/summary");
  }

  async getSelection(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/selection");
  }

  async getCameras(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/cameras");
  }

  async getToolbar(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/toolbar");
  }

  async getScene(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/scene");
  }

  async buildInfo(): Promise<unknown> {
    return this.requestJson("GET", "/api/build");
  }

  async pick(x: number, y: number): Promise<unknown> {
    return this.requestJson("POST", "/api/pick", { x, y });
  }

  async command(line: string): Promise<unknown> {
    return this.requestJson("POST", "/api/command", { line });
  }

  async waitFor(input: {
    contains?: string;
    resource?: string;
    timeoutMs?: number;
    intervalMs?: number;
  }): Promise<unknown> {
    const startedAt = Date.now();
    const timeoutMs = input.timeoutMs ?? 5_000;
    const intervalMs = input.intervalMs ?? 100;

    while (Date.now() - startedAt <= timeoutMs) {
      const value = await this.readResource(input.resource);
      const text = JSON.stringify(value);
      if (!input.contains || text.includes(input.contains)) {
        return {
          found: true,
          resource: normalizeResource(input.resource),
          elapsedMs: Date.now() - startedAt,
          value,
        };
      }
      await delay(intervalMs);
    }

    return {
      found: false,
      resource: normalizeResource(input.resource),
      elapsedMs: Date.now() - startedAt,
    };
  }

  async recordingStatus(): Promise<unknown> {
    return this.requestJson("GET", "/recording/status");
  }

  async recordingEnable(): Promise<unknown> {
    return this.requestJson("POST", "/recording/enable");
  }

  async recordingDisable(input: { force?: boolean } = {}): Promise<unknown> {
    return this.requestJson("POST", "/recording/disable", input);
  }

  async recordingStart(input: {
    detailLevel?: "basic" | "diagnostic" | "trace";
  } = {}): Promise<unknown> {
    return this.requestJson("POST", "/recording/start", input);
  }

  async recordingStop(input: { save?: boolean } = {}): Promise<unknown> {
    return this.requestJson("POST", "/recording/stop", input);
  }

  async recordingList(): Promise<unknown> {
    return this.requestJson("GET", "/recording/list");
  }

  async recordingRead(id: string): Promise<unknown> {
    return this.requestJson("GET", `/recording/read?id=${encodeURIComponent(id)}`);
  }

  async recordingReplay(input: { id?: string; path?: string } = {}): Promise<unknown> {
    return this.requestJson("POST", "/recording/replay", input);
  }

  async recordingProbe(
    target: "summary" | "selection" | "cameras" | "toolbar" | "scene" | "state",
  ): Promise<unknown> {
    return this.requestJson(
      "GET",
      `/recording/probe?target=${encodeURIComponent(target)}`,
    );
  }

  private async requestJson(
    method: "GET" | "POST",
    path: string,
    body?: unknown,
  ): Promise<unknown> {
    const headers: Record<string, string> = {
      "content-type": "application/json",
    };
    if (this.input.bearerToken) {
      headers.authorization = `Bearer ${this.input.bearerToken}`;
    }

    const response = await fetch(`${this.input.httpBaseUrl}${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(this.input.timeoutMs ?? 5_000),
    });
    if (!response.ok) {
      throw new Error(`editor request failed: ${response.status}`);
    }
    return response.json();
  }

  async readResource(resource: string | undefined): Promise<unknown> {
    switch (normalizeResource(resource)) {
      case "summary":
        return this.getSummary();
      case "selection":
        return this.getSelection();
      case "cameras":
        return this.getCameras();
      case "scene":
        return this.getScene();
      case "toolbar":
        return this.getToolbar();
      default:
        throw new Error(`unknown editor resource: ${resource}`);
    }
  }
}

function normalizeResource(resource: string | undefined): string {
  if (!resource || resource === "lxe-editor://summary") {
    return "summary";
  }
  return resource.replace(/^lxe-editor:\/\//, "");
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
