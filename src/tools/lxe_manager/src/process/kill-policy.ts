export class KillPolicy {
  constructor(
    private readonly input: {
      gracefulStop: () => Promise<void>;
      forceKill: () => Promise<void>;
      isRunning: () => boolean | Promise<boolean>;
      gracePeriodMs?: number;
      sleep?: (ms: number) => Promise<void>;
    },
  ) {}

  async terminate(): Promise<void> {
    await this.input.gracefulStop();
    const sleep = this.input.sleep ?? defaultSleep;
    await sleep(this.input.gracePeriodMs ?? 3000);
    if (await this.input.isRunning()) {
      await this.input.forceKill();
    }
  }
}

function defaultSleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
