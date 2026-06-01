import { render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { App } from "./App";

describe("App", () => {
  beforeEach(() => {
    vi.stubGlobal("fetch", vi.fn(async (url: string) => {
      if (url === "/api/sources") {
        return jsonResponse({
          schemaVersion: 1,
          sources: [{
            id: "polyhaven",
            name: "Poly Haven",
            url: "https://polyhaven.com/",
            description: "CC0 HDRI source",
            licensePolicy: "CC0 only",
            categories: [{
              id: "hdri",
              name: "HDRI",
              recommended: [{
                id: "studio_small_03",
                displayName: "Studio Small 03",
                url: "https://polyhaven.com/a/studio_small_03",
                sourceUrl: "https://polyhaven.com/a/studio_small_03",
                expectedLicense: "CC0",
                licenseStatus: "verified",
                licenseUrl: "https://polyhaven.com/license",
                kind: "environment",
                variants: ["2k-hdr"],
                recipe: { outputKind: "environment", downloadStrategy: "direct-url", conversion: "copy-environment" },
                manualNotes: "Use direct HDR download"
              }]
            }]
          }]
        });
      }
      if (url === "/api/cache") {
        return jsonResponse({ assets: [] });
      }
      return jsonResponse({ error: "unexpected" }, false);
    }));
  });

  it("lists default sources, filters, recommendations, and license state", async () => {
    render(<App />);

    await waitFor(() => expect(screen.getByRole("heading", { name: "Poly Haven" })).toBeTruthy());
    expect(screen.getByText("HDRI")).toBeTruthy();
    expect(screen.getByText("Studio Small 03")).toBeTruthy();
    expect(screen.getByText("verified · CC0")).toBeTruthy();
    expect(screen.getByText("Cache Browser")).toBeTruthy();
  });
});

function jsonResponse(body: unknown, ok = true): Response {
  return {
    ok,
    statusText: ok ? "OK" : "Bad Request",
    json: async () => body
  } as Response;
}
