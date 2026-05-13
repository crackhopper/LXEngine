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

  private async readResource(resource: string | undefined): Promise<unknown> {
    switch (normalizeResource(resource)) {
      case "summary":
        return this.getSummary();
      case "selection":
        return this.getSelection();
      case "cameras":
        return this.getCameras();
      case "scene":
        return this.requestJson("GET", "/api/state/scene");
      case "toolbar":
        return this.requestJson("GET", "/api/state/toolbar");
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
