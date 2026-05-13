export interface ToolResult {
  content: Array<{ type: "text"; text: string }>;
  isError?: boolean;
}

export type ToolArguments = Record<string, unknown>;

export type ToolHandler = (args: ToolArguments) => Promise<ToolResult>;
