export interface OperationRecord {
  name: string;
  ok: boolean;
  summary: string;
}

export class OperationLog {
  private readonly records: OperationRecord[] = [];

  push(record: OperationRecord): void {
    this.records.unshift(record);
    this.records.splice(50);
  }

  latest(): OperationRecord | undefined {
    return this.records[0];
  }
}
