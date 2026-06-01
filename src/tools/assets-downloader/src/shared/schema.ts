import { z } from "zod";

export const assetKindSchema = z.enum(["environment", "model", "material", "scene", "course-asset", "point-cloud"]);
export const categoryIdSchema = z.enum(["hdri", "model", "material", "scene", "course-asset", "point-cloud"]);
export const licenseStatusSchema = z.enum(["verified", "user_confirmed", "blocked", "unknown"]);
export const outputKindSchema = z.enum(["environment", "model", "material", "point-cloud", "manual"]);

export const recipeSchema = z.object({
  outputKind: outputKindSchema,
  downloadStrategy: z.string().min(1),
  conversion: z.string().min(1)
});

export const recommendedAssetSchema = z.object({
  id: z.string().min(1),
  displayName: z.string().min(1),
  url: z.string().url(),
  sourceUrl: z.string().url(),
  expectedLicense: z.string().min(1),
  licenseStatus: licenseStatusSchema,
  licenseUrl: z.string().url().optional(),
  kind: assetKindSchema,
  variants: z.array(z.string().min(1)).min(1),
  recipe: recipeSchema,
  manualNotes: z.string().min(1)
});

export const categorySchema = z.object({
  id: categoryIdSchema,
  name: z.string().min(1),
  recommended: z.array(recommendedAssetSchema)
});

export const sourceSchema = z.object({
  id: z.string().regex(/^[a-z0-9][a-z0-9-]*$/),
  name: z.string().min(1),
  url: z.string().url(),
  description: z.string().min(1),
  licensePolicy: z.string().min(1),
  categories: z.array(categorySchema).min(1)
});

export const catalogSchema = z.object({
  schemaVersion: z.literal(1),
  sources: z.array(sourceSchema).min(1)
});

export const userConfirmedLicenseSchema = z.object({
  licenseName: z.string().min(1),
  licenseUrl: z.string().url().optional(),
  confirmationSource: z.string().min(1)
});

export const importPreviewRequestSchema = z.object({
  url: z.string().min(1),
  sourceId: z.string().optional(),
  assetId: z.string().optional(),
  variant: z.string().optional(),
  kind: outputKindSchema.optional(),
  userConfirmedLicense: userConfirmedLicenseSchema.optional()
});

export const importStartRequestSchema = importPreviewRequestSchema.extend({
  previewId: z.string().optional()
});

export const convertedOutputSchema = z.object({
  kind: outputKindSchema,
  label: z.string().min(1),
  uri: z.string().startsWith("cache://"),
  relativePath: z.string().min(1)
});

export const importPreviewPlanSchema = z.object({
  previewId: z.string().min(1),
  url: z.string().min(1),
  sourceId: z.string().min(1),
  sourceName: z.string().min(1),
  assetId: z.string().min(1),
  variant: z.string().min(1),
  kind: outputKindSchema,
  licenseStatus: licenseStatusSchema,
  licenseName: z.string().min(1),
  licenseUrl: z.string().optional(),
  importAllowed: z.boolean(),
  cacheRoot: z.string().min(1),
  cachePath: z.string().min(1),
  cacheUriBase: z.string().startsWith("cache://"),
  filesToWrite: z.array(z.string().min(1)),
  manualNotes: z.string()
});

export const jobStatusSchema = z.enum(["queued", "running", "completed", "failed"]);

export const importJobSchema = z.object({
  id: z.string().min(1),
  status: jobStatusSchema,
  plan: importPreviewPlanSchema,
  logs: z.array(z.string()),
  error: z.string().optional(),
  metadata: z.unknown().optional()
});

export const cacheAssetMetadataSchema = z.object({
  schemaVersion: z.literal(1),
  displayName: z.string().min(1),
  kind: outputKindSchema,
  source: z.object({
    id: z.string().min(1),
    name: z.string().min(1),
    url: z.string().min(1)
  }),
  asset: z.object({
    id: z.string().min(1),
    variant: z.string().min(1),
    cacheUriBase: z.string().startsWith("cache://")
  }),
  license: z.object({
    status: licenseStatusSchema,
    name: z.string().min(1),
    url: z.string().optional(),
    file: z.string().optional(),
    confirmedAt: z.string().optional(),
    confirmationSource: z.string().optional()
  }),
  download: z.object({
    url: z.string().min(1),
    downloadedAt: z.string(),
    contentHash: z.string().min(1)
  }),
  convertedOutputs: z.array(convertedOutputSchema),
  manualNotes: z.string()
});

export type AssetKind = z.infer<typeof assetKindSchema>;
export type Catalog = z.infer<typeof catalogSchema>;
export type Source = z.infer<typeof sourceSchema>;
export type RecommendedAsset = z.infer<typeof recommendedAssetSchema>;
export type LicenseStatus = z.infer<typeof licenseStatusSchema>;
export type OutputKind = z.infer<typeof outputKindSchema>;
export type ImportPreviewRequest = z.infer<typeof importPreviewRequestSchema>;
export type ImportStartRequest = z.infer<typeof importStartRequestSchema>;
export type ImportPreviewPlan = z.infer<typeof importPreviewPlanSchema>;
export type ImportJob = z.infer<typeof importJobSchema>;
export type CacheAssetMetadata = z.infer<typeof cacheAssetMetadataSchema>;
