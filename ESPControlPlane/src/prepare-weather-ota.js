import path from "node:path";
import { fileURLToPath } from "node:url";
import { prepareWeatherOta, weatherDefaultOptions } from "./weather-ota.js";

function parseArgs(argv) {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const args = weatherDefaultOptions(here);

  for (let i = 2; i < argv.length; i += 1) {
    const token = argv[i];
    const next = argv[i + 1];
    if (token === "--module-path" && next) {
      args.modulePath = path.resolve(here, next);
      i += 1;
    } else if (token === "--module-url" && next) {
      args.moduleUrl = next;
      i += 1;
    } else if (token === "--version" && next) {
      args.version = next;
      i += 1;
    } else if (token === "--min-host-abi" && next) {
      args.minHostAbi = Number(next) || 1;
      i += 1;
    } else if (token === "--assign" && next) {
      args.assign = next;
      i += 1;
    } else if (token === "--device-id" && next) {
      args.deviceId = Number(next);
      i += 1;
    }
  }

  return args;
}

async function main() {
  const args = parseArgs(process.argv);
  const result = await prepareWeatherOta(args);
  console.log(JSON.stringify(result, null, 2));
}

main().catch((error) => {
  console.error(String(error?.message || error));
  process.exit(1);
});
