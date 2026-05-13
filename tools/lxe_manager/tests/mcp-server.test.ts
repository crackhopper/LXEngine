import { describe, expect, it, vi } from "vitest";
import { createToolHandlers } from "../src/mcp/server.js";

describe("mcp tool handlers", () => {
  it("routes editor summary to the editor client", async () => {
    const getSummary = vi.fn(async () => ({
      sceneName: "Scene",
      dirty: false,
    }));
    const handlers = createToolHandlers({
      editorOps: {
        status: vi.fn(async () => ({ running: true })),
      },
      editorClient: {
        getSummary,
      },
      workspaceOps: {
        repoPull: vi.fn(),
      },
    });

    await expect(handlers["editor.get_summary"]()).resolves.toEqual({
      content: [{ type: "text", text: '{"sceneName":"Scene","dirty":false}' }],
    });
    expect(getSummary).toHaveBeenCalledOnce();
  });

  it("routes repo pull to workspace ops", async () => {
    const repoPull = vi.fn(async () => ({
      exitCode: 0,
      stdout: "ok",
      stderr: "",
    }));
    const handlers = createToolHandlers({
      editorOps: { status: vi.fn() },
      editorClient: { getSummary: vi.fn() },
      workspaceOps: { repoPull },
    });

    await expect(handlers["ops.repo_pull"]()).resolves.toEqual({
      content: [
        {
          type: "text",
          text: '{"exitCode":0,"stdout":"ok","stderr":""}',
        },
      ],
    });
    expect(repoPull).toHaveBeenCalledOnce();
  });
});
