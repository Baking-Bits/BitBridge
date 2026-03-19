import {
  ActionRowBuilder,
  ButtonBuilder,
  ButtonStyle,
  Client,
  EmbedBuilder,
  Events,
  GatewayIntentBits
} from "discord.js";
import { config } from "./config.js";
import { getContainerStatus, restartContainer, startContainer } from "./docker.js";
import { fetchJellyfinStatus, fetchJellyseerrStatus } from "./services.js";

const STATUS = {
  jellyfin: { messageId: null },
  jellyseerr: { messageId: null }
};

const client = new Client({
  intents: [GatewayIntentBits.Guilds]
});

function relativeTimestamp(date = new Date()) {
  return `<t:${Math.floor(date.getTime() / 1000)}:R>`;
}

function jellyfinComponents() {
  return [
    new ActionRowBuilder().addComponents(
      new ButtonBuilder().setCustomId("jellyfin_start").setLabel("Start").setStyle(ButtonStyle.Secondary),
      new ButtonBuilder().setCustomId("jellyfin_restart").setLabel("Restart Jellyfin").setStyle(ButtonStyle.Danger)
    )
  ];
}

function jellyseerrComponents() {
  return [
    new ActionRowBuilder().addComponents(
      new ButtonBuilder().setCustomId("jellyseerr_start").setLabel("Start").setStyle(ButtonStyle.Secondary),
      new ButtonBuilder().setCustomId("jellyseerr_restart").setLabel("Restart Jellyseerr").setStyle(ButtonStyle.Danger)
    )
  ];
}

function buildJellyfinEmbed(appStatus, containerStatus) {
  const onlineDot = appStatus.online ? "🟢" : "🔴";
  const containerDot = containerStatus.online ? "🟢" : "🔴";

  return new EmbedBuilder()
    .setTitle("🎬 Jellyfin Media Server")
    .setColor(appStatus.online ? 0x2ecc71 : 0xe74c3c)
    .setDescription(`${onlineDot} App: **${appStatus.online ? "Online" : "Offline"}** · ${containerDot} Container: **${containerStatus.statusText}**`)
    .addFields(
      { name: "Active Streams", value: String(appStatus.activeStreams ?? 0), inline: true },
      { name: "Sessions", value: String(appStatus.sessions ?? 0), inline: true },
      { name: "Version", value: String(appStatus.version ?? "unknown"), inline: true }
    )
    .setFooter({ text: `Updated ${new Date().toLocaleString()}` });
}

function buildJellyseerrEmbed(appStatus, containerStatus) {
  const onlineDot = appStatus.online ? "🟢" : "🔴";
  const containerDot = containerStatus.online ? "🟢" : "🔴";
  const movie = appStatus.movie || {};
  const tv = appStatus.tv || {};

  return new EmbedBuilder()
    .setTitle("🍿 Jellyseerr")
    .setColor(appStatus.online ? 0x2ecc71 : 0xe74c3c)
    .setDescription(`${onlineDot} App: **${appStatus.online ? "Online" : "Offline"}** · ${containerDot} Container: **${containerStatus.statusText}**`)
    .addFields(
      {
        name: "Movies",
        value: `Pending: ${movie.pending || 0}\nQueue: ${movie.processing || 0}\nIn Library: ${movie.available || 0}`,
        inline: true
      },
      {
        name: "TV",
        value: `Pending: ${tv.pending || 0}\nQueue: ${tv.processing || 0}\nIn Library: ${tv.available || 0}`,
        inline: true
      },
      {
        name: "All",
        value: `Pending: ${(movie.pending || 0) + (tv.pending || 0)}\nQueue: ${(movie.processing || 0) + (tv.processing || 0)}\nIn Library: ${(movie.available || 0) + (tv.available || 0)}`,
        inline: true
      }
    )
    .setFooter({ text: `Updated ${new Date().toLocaleString()}` });
}

async function upsertStatusMessage(channelId, statusKey, payload) {
  const channel = await client.channels.fetch(channelId);
  if (!channel?.isTextBased()) {
    throw new Error(`Channel ${channelId} is not text-based or unavailable`);
  }

  const status = STATUS[statusKey];
  if (status.messageId) {
    try {
      const message = await channel.messages.fetch(status.messageId);
      await message.edit(payload);
      return;
    } catch (_error) {
      status.messageId = null;
    }
  }

  const sent = await channel.send(payload);
  status.messageId = sent.id;
}

async function updateJellyfinStatus() {
  const [appStatus, containerStatus] = await Promise.all([
    fetchJellyfinStatus(config.jellyfin),
    getContainerStatus(config.unraid, config.unraid.containers.jellyfin)
  ]);

  const embed = buildJellyfinEmbed(appStatus, containerStatus);
  await upsertStatusMessage(config.jellyfin.channelId, "jellyfin", {
    content: `Updated ${relativeTimestamp()}`,
    embeds: [embed],
    components: jellyfinComponents()
  });
}

async function updateJellyseerrStatus() {
  const [appStatus, containerStatus] = await Promise.all([
    fetchJellyseerrStatus(config.jellyseerr),
    getContainerStatus(config.unraid, config.unraid.containers.jellyseerr)
  ]);

  const embed = buildJellyseerrEmbed(appStatus, containerStatus);
  await upsertStatusMessage(config.jellyseerr.channelId, "jellyseerr", {
    content: `Updated ${relativeTimestamp()}`,
    embeds: [embed],
    components: jellyseerrComponents()
  });
}

async function runWithErrorLog(task, label) {
  try {
    await task();
  } catch (error) {
    console.error(`[${label}]`, error.message || error);
  }
}

client.on(Events.InteractionCreate, async (interaction) => {
  if (!interaction.isButton()) return;

  const id = interaction.customId;
  const isJellyfin = id.startsWith("jellyfin_");
  const isJellyseerr = id.startsWith("jellyseerr_");
  if (!isJellyfin && !isJellyseerr) return;

  const service = isJellyfin ? "jellyfin" : "jellyseerr";
  const container = isJellyfin ? config.unraid.containers.jellyfin : config.unraid.containers.jellyseerr;
  const action = id.endsWith("_restart") ? "restart" : "start";

  await interaction.deferReply({ ephemeral: true });

  try {
    if (action === "restart") {
      await restartContainer(config.unraid, container);
    } else {
      await startContainer(config.unraid, container);
    }

    await interaction.editReply(`${action.toUpperCase()} sent for ${container}`);

    if (service === "jellyfin") {
      await runWithErrorLog(updateJellyfinStatus, "jellyfin-refresh-after-action");
    } else {
      await runWithErrorLog(updateJellyseerrStatus, "jellyseerr-refresh-after-action");
    }
  } catch (error) {
    await interaction.editReply(`Failed: ${error.message || error}`);
  }
});

client.once(Events.ClientReady, async (readyClient) => {
  console.log(`Logged in as ${readyClient.user.tag}`);

  await runWithErrorLog(updateJellyfinStatus, "jellyfin-initial");
  await runWithErrorLog(updateJellyseerrStatus, "jellyseerr-initial");

  setInterval(() => runWithErrorLog(updateJellyfinStatus, "jellyfin-interval"), config.jellyfin.intervalMs);
  setInterval(() => runWithErrorLog(updateJellyseerrStatus, "jellyseerr-interval"), config.jellyseerr.intervalMs);
});

client.login(config.discordToken);
