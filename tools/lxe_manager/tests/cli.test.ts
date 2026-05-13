import { describe, expect, it } from "vitest";
import { parseManagerCliOptions } from "../src/cli.js";

describe("manager cli options", () => {
  const generateToken = () => "generated-token";

  it("uses loopback defaults and generates a token", () => {
    expect(parseManagerCliOptions([], {}, generateToken)).toMatchObject({
      host: "127.0.0.1",
      port: 3880,
      bearerToken: "generated-token",
      bearerTokenGenerated: true,
    });
  });

  it("accepts startup arguments for host and port", () => {
    expect(
      parseManagerCliOptions(
        [
          "--host",
          "0.0.0.0",
          "--port",
          "3999",
          "--repo-root",
          "/repo",
          "--runtime-root",
          "/runtime",
          "--editor-executable",
          "/repo/build/lxe_editor",
          "--token",
          "secret",
        ],
        {},
        generateToken,
      ),
    ).toEqual({
      host: "0.0.0.0",
      port: 3999,
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
      editorExecutable: "/repo/build/lxe_editor",
      bearerToken: "secret",
      bearerTokenGenerated: false,
    });
  });

  it("falls back to environment values", () => {
    expect(
      parseManagerCliOptions([], {
        LXE_MANAGER_HOST: "127.0.0.1",
        LXE_MANAGER_PORT: "3999",
        LXE_MANAGER_REPO_ROOT: "/repo",
        LXE_MANAGER_RUNTIME_ROOT: "/runtime",
        LXE_MANAGER_EDITOR_EXECUTABLE: "/repo/editor",
        LXE_MANAGER_MCP_BEARER_TOKEN: "secret",
      }),
    ).toEqual({
      host: "127.0.0.1",
      port: 3999,
      repoRoot: "/repo",
      runtimeRoot: "/runtime",
      editorExecutable: "/repo/editor",
      bearerToken: "secret",
      bearerTokenGenerated: false,
    });
  });

  it("generates a token for non-loopback bindings", () => {
    expect(
      parseManagerCliOptions(["--host", "0.0.0.0"], {}, generateToken),
    ).toMatchObject({
      host: "0.0.0.0",
      bearerToken: "generated-token",
      bearerTokenGenerated: true,
    });
  });

  it("allows non-loopback bindings when a token is provided by the environment", () => {
    expect(
      parseManagerCliOptions(["--host", "0.0.0.0"], {
        LXE_MANAGER_MCP_BEARER_TOKEN: "secret",
      }),
    ).toMatchObject({
      host: "0.0.0.0",
      port: 3880,
      bearerToken: "secret",
      bearerTokenGenerated: false,
    });
  });

  it("accepts documented MCP aliases", () => {
    expect(
      parseManagerCliOptions(
        ["--mcp-host", "0.0.0.0", "--mcp-port", "3999", "--bearer-token", "secret"],
        {},
        generateToken,
      ),
    ).toMatchObject({
      host: "0.0.0.0",
      port: 3999,
      bearerToken: "secret",
      bearerTokenGenerated: false,
    });
  });

  it("rejects unknown arguments and invalid ports", () => {
    expect(() => parseManagerCliOptions(["--unknown"], {})).toThrow(
      "unknown lxe_manager argument",
    );
    expect(() =>
      parseManagerCliOptions(["--port", "70000"], {}),
    ).toThrow("invalid --port");
  });
});
