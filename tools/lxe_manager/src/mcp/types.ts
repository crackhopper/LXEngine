export interface ToolResult {
  content: Array<
    | { type: "text"; text: string }
    | { type: "image"; data: string; mimeType: string }
  >;
  isError?: boolean;
}

export type ToolArguments = Record<string, unknown>;

export type ToolHandler = (args: ToolArguments) => Promise<ToolResult>;
