import path from "node:path";
import { deployLocalWasmModule } from "./wasm-deploy.js";

export function weatherDefaultOptions(baseDir) {
  return {
    modulePath: path.resolve(baseDir, "../../packages/wasm/weather-v1.wasm"),
    moduleUrl: "/packages/wasm/weather-v1.wasm",
    version: "1.0.0",
    minHostAbi: 1,
    assign: "none",
    deviceId: null
  };
}

export async function prepareWeatherOta(options) {
  const modulePath = options?.modulePath;
  const moduleUrl = options?.moduleUrl || "/packages/wasm/weather-v1.wasm";
  const version = options?.version || "1.0.0";
  const minHostAbi = Number(options?.minHostAbi) || 1;
  const assign = options?.assign || "none";
  const deviceId = options?.deviceId == null ? null : Number(options.deviceId);

  const result = await deployLocalWasmModule({
    staticRoot: path.resolve(path.dirname(modulePath), "..", ".."),
    appType: "weather",
    fileName: path.basename(modulePath),
    version,
    minHostAbi,
    assign,
    deviceId,
    notes: "Weather OTA module (Aura-style + auto IP location)"
  });

  return {
    ...result,
    moduleFile: modulePath,
    activeWeatherModule: {
      appType: result.appType,
      version: result.version,
      moduleUrl: result.moduleUrl,
      checksumSha256: result.checksumSha256,
      minHostAbi
    }
  };
}
