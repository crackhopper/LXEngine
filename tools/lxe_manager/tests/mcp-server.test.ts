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

  it("returns a structured guardian error when a task is killed", async () => {
    const handlers = createToolHandlers({
      editorOps: { status: async () => ({ running: false }) },
      editorClient: { getSummary: async () => ({}) },
      workspaceOps: {
        repoPull: async () => {
          throw new Error("killed_by_guardian: cpu=390 rss=4096 free_mem=16");
        },
      },
    });

    await expect(handlers["ops.repo_pull"]()).rejects.toThrow(
      "ops.repo_pull failed: killed_by_guardian: cpu=390 rss=4096 free_mem=16",
    );
  });
});
