import type { ToolResult } from "./types.js";

export function createToolHandlers(input: {
  editorOps: { status: () => Promise<unknown> };
  editorClient: { getSummary: () => Promise<unknown> };
  workspaceOps: { repoPull: () => Promise<unknown> };
}): Record<string, () => Promise<ToolResult>> {
  return {
    "editor.get_summary": async () => jsonText(await input.editorClient.getSummary()),
    "ops.repo_pull": async () => jsonText(await input.workspaceOps.repoPull()),
    "ops.editor_status": async () => jsonText(await input.editorOps.status()),
  };
}

function jsonText(value: unknown): ToolResult {
  return {
    content: [{ type: "text", text: JSON.stringify(value) }],
  };
}
