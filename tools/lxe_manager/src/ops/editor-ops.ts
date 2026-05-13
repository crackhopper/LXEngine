export interface EditorStatus {
  running: boolean;
  pid?: number;
}

export class EditorOps {
  async status(): Promise<EditorStatus> {
    return { running: false };
  }
}
