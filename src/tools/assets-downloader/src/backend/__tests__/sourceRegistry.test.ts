// @vitest-environment node

import { describe, expect, it } from "vitest";
import { catalogSchema } from "../../shared/schema";
import { loadCatalog, validateDefaultCatalog } from "../services/sourceRegistry";

describe("source registry", () => {
  it("loads the default catalog with verified recommended entries and recipes", async () => {
    const catalog = await loadCatalog();
    expect(catalog.sources.map((source) => source.id)).toContain("polyhaven");
    expect(catalog.sources.map((source) => source.id)).toContain("games-course-assets");

    const recommended = catalog.sources.flatMap((source) =>
      source.categories.flatMap((category) => category.recommended)
    );
    expect(recommended.length).toBeGreaterThan(0);
    for (const entry of recommended) {
      expect(entry.licenseStatus).toBe("verified");
      expect(entry.recipe.conversion.length).toBeGreaterThan(0);
    }
  });

  it("rejects unknown or blocked default recommended assets", () => {
    const badCatalog = catalogSchema.parse({
      schemaVersion: 1,
      sources: [{
        id: "bad-source",
        name: "Bad Source",
        url: "https://example.com/",
        description: "bad",
        licensePolicy: "bad",
        categories: [{
          id: "material",
          name: "Material",
          recommended: [{
            id: "bad",
            displayName: "Bad",
            url: "https://example.com/bad",
            sourceUrl: "https://example.com/bad",
            expectedLicense: "unknown",
            licenseStatus: "unknown",
            kind: "material",
            variants: ["1k"],
            recipe: { outputKind: "material", downloadStrategy: "direct-url", conversion: "pbr-texture-set" },
            manualNotes: "bad"
          }]
        }]
      }]
    });

    expect(() => validateDefaultCatalog(badCatalog)).toThrow(/must be verified/);
  });
});
