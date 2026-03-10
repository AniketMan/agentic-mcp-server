/**
 * Unit tests for AgenticMCP VisualAgent CLI tool
 */

import { describe, it, expect, vi, beforeEach } from "vitest";
import { parseCommand, executeVisualAgentCommand, getHelpText } from "../../visual-agent.js";

describe("parseCommand", () => {
  it("parses simple command", () => {
    const result = parseCommand("screenshot");
    expect(result.action).toBe("screenshot");
    expect(result.args).toEqual([]);
    expect(result.flags).toEqual({});
  });

  it("parses command with args", () => {
    const result = parseCommand("focus a0");
    expect(result.action).toBe("focus");
    expect(result.args).toEqual(["a0"]);
  });

  it("parses command with flags", () => {
    const result = parseCommand("screenshot --format=png --width=1920");
    expect(result.action).toBe("screenshot");
    expect(result.flags.format).toBe("png");
    expect(result.flags.width).toBe("1920");
  });

  it("parses command with boolean flags", () => {
    const result = parseCommand("select a1 --add");
    expect(result.action).toBe("select");
    expect(result.args).toEqual(["a1"]);
    expect(result.flags.add).toBe(true);
  });

  it("parses command with quoted arguments", () => {
    const result = parseCommand('spawn StaticMeshActor 100 200 300 --label="My Actor"');
    expect(result.action).toBe("spawn");
    expect(result.args).toEqual(["StaticMeshActor", "100", "200", "300"]);
    expect(result.flags.label).toBe("My Actor");
  });

  it("parses camera command with multiple numeric args", () => {
    const result = parseCommand("camera 0 0 500 -45 0 0");
    expect(result.action).toBe("camera");
    expect(result.args).toEqual(["0", "0", "500", "-45", "0", "0"]);
  });

  it("handles empty command", () => {
    const result = parseCommand("");
    expect(result.action).toBe(null);
  });

  it("handles whitespace-only command", () => {
    const result = parseCommand("   ");
    expect(result.action).toBe(null);
  });
});

describe("executeVisualAgentCommand", () => {
  let mockHttpClient;

  beforeEach(() => {
    mockHttpClient = vi.fn();
  });

  it("executes screenshot command", async () => {
    mockHttpClient.mockResolvedValueOnce({
      success: true,
      width: 1280,
      height: 720,
      format: "jpeg",
      data: "base64data",
    });

    const result = await executeVisualAgentCommand("screenshot", mockHttpClient);

    expect(mockHttpClient).toHaveBeenCalledWith("screenshot", {
      width: 1280,
      height: 720,
      format: "jpeg",
      quality: 75,
    });
    expect(result.success).toBe(true);
  });

  it("executes snapshot command", async () => {
    mockHttpClient.mockResolvedValueOnce({
      success: true,
      actorCount: 5,
      yamlSnapshot: "- Actor [a0]",
    });

    const result = await executeVisualAgentCommand("snapshot", mockHttpClient);

    expect(mockHttpClient).toHaveBeenCalledWith("sceneSnapshot", {
      classFilter: "",
      includeComponents: true,
    });
    expect(result.success).toBe(true);
  });

  it("executes focus command", async () => {
    mockHttpClient
      .mockResolvedValueOnce({ success: true, message: "Focused" })
      .mockResolvedValueOnce({ success: true, yamlSnapshot: "" }); // For auto-snapshot

    const result = await executeVisualAgentCommand("focus a0", mockHttpClient);

    expect(mockHttpClient).toHaveBeenCalledWith("focusActor", { name: "a0" });
    expect(result.success).toBe(true);
  });

  it("executes spawn command with all parameters", async () => {
    mockHttpClient
      .mockResolvedValueOnce({ success: true, message: "Spawned" })
      .mockResolvedValueOnce({ success: true, yamlSnapshot: "" });

    const result = await executeVisualAgentCommand(
      'spawn PointLight 100 200 300 --label="MyLight"',
      mockHttpClient
    );

    expect(mockHttpClient).toHaveBeenCalledWith("spawnActor", {
      className: "PointLight",
      locationX: 100,
      locationY: 200,
      locationZ: 300,
      label: "MyLight",
    });
  });

  it("returns error for unknown command", async () => {
    const result = await executeVisualAgentCommand("unknownCommand", mockHttpClient);

    expect(result.success).toBe(false);
    expect(result.message).toContain("Unknown command");
  });

  it("returns error for missing arguments", async () => {
    const result = await executeVisualAgentCommand("focus", mockHttpClient);

    expect(result.success).toBe(false);
    expect(result.message).toContain("Usage:");
  });

  it("handles record start/stop", async () => {
    const startResult = await executeVisualAgentCommand("record start", mockHttpClient);
    expect(startResult.success).toBe(true);
    expect(startResult.message).toContain("Recording started");

    const stopResult = await executeVisualAgentCommand("record stop", mockHttpClient);
    expect(stopResult.success).toBe(true);
    expect(stopResult.message).toContain("Recording stopped");
  });

  it("returns help text", async () => {
    const result = await executeVisualAgentCommand("help", mockHttpClient);

    expect(result.success).toBe(true);
    expect(result.message).toContain("unreal_cli");
    expect(result.message).toContain("screenshot");
    expect(result.message).toContain("snapshot");
  });
});

describe("getHelpText", () => {
  it("contains all major command categories", () => {
    const help = getHelpText();

    expect(help).toContain("Visual Commands");
    expect(help).toContain("Actor Commands");
    expect(help).toContain("Viewport Commands");
    expect(help).toContain("Recording Commands");
  });

  it("contains example commands", () => {
    const help = getHelpText();

    expect(help).toContain("screenshot");
    expect(help).toContain("snapshot");
    expect(help).toContain("focus");
    expect(help).toContain("spawn");
    expect(help).toContain("record");
  });
});
