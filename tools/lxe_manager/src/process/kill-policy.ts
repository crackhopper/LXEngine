export class KillPolicy {
  constructor(
    private readonly gracefulStop: () => Promise<void>,
    private readonly forceKill: () => Promise<void>,
  ) {}

  async terminate(): Promise<void> {
    await this.gracefulStop();
    await new Promise((resolve) => setTimeout(resolve, 3000));
    await this.forceKill();
  }
}
