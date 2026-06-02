// @vitest-environment node

import { describe, expect, it } from "vitest";
import { buildServer } from "../server";

describe("assets-downloader server", () => {
  it("exposes one composed buildInfo field", async () => {
    const app = await buildServer();
    const response = await app.inject({
      method: "GET",
      url: "/api/build-info"
    });

    expect(response.statusCode).toBe(200);
    const body = response.json() as { buildInfo?: string; gitCommit?: string };
    expect(body.buildInfo).toContain("assets-downloader");
    expect(body.gitCommit).toBeUndefined();
    await app.close();
  });
});
