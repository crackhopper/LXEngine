export class EditorClient {
  constructor(
    private readonly input: {
      httpBaseUrl: string;
      bearerToken?: string;
    },
  ) {}

  async health(): Promise<unknown> {
    return this.requestJson("GET", "/health");
  }

  async getSummary(): Promise<unknown> {
    return this.requestJson("GET", "/api/state/summary");
  }

  async command(line: string): Promise<unknown> {
    return this.requestJson("POST", "/api/command", { line });
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
    });
    if (!response.ok) {
      throw new Error(`editor request failed: ${response.status}`);
    }
    return response.json();
  }
}
