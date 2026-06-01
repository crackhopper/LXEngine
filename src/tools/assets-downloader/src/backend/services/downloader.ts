import crypto from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";

export interface DownloadResult {
  filePath: string;
  contentHash: string;
}

export async function downloadToFile(url: string, destinationPath: string): Promise<DownloadResult> {
  await fs.mkdir(path.dirname(destinationPath), { recursive: true });
  if (url.startsWith("file://")) {
    const sourcePath = new URL(url);
    await fs.copyFile(sourcePath, destinationPath);
    return { filePath: destinationPath, contentHash: await hashFile(destinationPath) };
  }

  const response = await fetch(url);
  if (!response.ok || !response.body) {
    throw new Error(`Download failed: ${response.status} ${response.statusText}`);
  }

  const buffer = Buffer.from(await response.arrayBuffer());
  await fs.writeFile(destinationPath, buffer);
  return { filePath: destinationPath, contentHash: hashBuffer(buffer) };
}

export async function hashFile(filePath: string): Promise<string> {
  const buffer = await fs.readFile(filePath);
  return hashBuffer(buffer);
}

function hashBuffer(buffer: Buffer): string {
  return crypto.createHash("sha256").update(buffer).digest("hex");
}
