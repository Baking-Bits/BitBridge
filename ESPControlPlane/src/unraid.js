import { Client as SSHClient } from "ssh2";

const SAFE_CONTAINER_NAME = /^[a-zA-Z0-9_.-]+$/;

function assertContainerName(containerName) {
  if (!SAFE_CONTAINER_NAME.test(containerName || "")) {
    throw new Error("Invalid container name");
  }
}

function runSshCommand(unraidConfig, command) {
  return new Promise((resolve, reject) => {
    const client = new SSHClient();
    let stdout = "";
    let stderr = "";

    client
      .on("ready", () => {
        client.exec(command, (err, stream) => {
          if (err) {
            client.end();
            reject(err);
            return;
          }

          stream
            .on("close", (code) => {
              client.end();
              if (code === 0) {
                resolve({ stdout: stdout.trim(), stderr: stderr.trim() });
              } else {
                reject(new Error(`Command failed (${code}): ${stderr || stdout || "unknown error"}`));
              }
            })
            .on("data", (data) => {
              stdout += data.toString();
            });

          stream.stderr.on("data", (data) => {
            stderr += data.toString();
          });
        });
      })
      .on("error", (err) => {
        reject(err);
      })
      .connect({
        host: unraidConfig.host,
        port: unraidConfig.port,
        username: unraidConfig.username,
        privateKey: unraidConfig.privateKey,
        passphrase: unraidConfig.privateKeyPassphrase || undefined,
        readyTimeout: 15_000
      });
  });
}

export async function restartContainer(unraidConfig, containerName) {
  assertContainerName(containerName);
  return runSshCommand(unraidConfig, `docker restart ${containerName}`);
}

export async function startContainer(unraidConfig, containerName) {
  assertContainerName(containerName);
  return runSshCommand(unraidConfig, `docker start ${containerName}`);
}

export async function getContainerStatus(unraidConfig, containerName) {
  assertContainerName(containerName);

  const result = await runSshCommand(
    unraidConfig,
    `docker ps -a --filter name=^/${containerName}$ --format "{{.Names}}|{{.Status}}"`
  );

  const line = (result.stdout || "").split("\n").find(Boolean);
  if (!line) {
    return { exists: false, statusText: "not found", online: false };
  }

  const [, statusText = "unknown"] = line.split("|");
  const online = statusText.toLowerCase().startsWith("up");
  return { exists: true, statusText, online };
}
