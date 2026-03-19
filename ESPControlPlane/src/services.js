import axios from "axios";

function client(baseURL, headers = {}) {
  return axios.create({
    baseURL,
    timeout: 12_000,
    headers
  });
}

export async function fetchJellyfinStatus(jellyfinConfig) {
  if (!jellyfinConfig.baseUrl || !jellyfinConfig.apiKey) {
    return {
      configured: false,
      online: false,
      name: "Jellyfin",
      version: "unconfigured",
      activeStreams: 0,
      sessions: 0,
      error: "Jellyfin configuration missing"
    };
  }

  const api = client(jellyfinConfig.baseUrl, {
    "X-Emby-Token": jellyfinConfig.apiKey
  });

  try {
    const [systemRes, sessionsRes] = await Promise.all([
      api.get("/System/Info/Public"),
      api.get("/Sessions")
    ]);

    const sessions = Array.isArray(sessionsRes.data) ? sessionsRes.data : [];
    const activeStreams = sessions.filter(
      (session) => session?.NowPlayingItem && !session?.PlayState?.IsPaused
    ).length;

    return {
      configured: true,
      online: true,
      name: systemRes.data?.ServerName || "Jellyfin",
      version: systemRes.data?.Version || "unknown",
      activeStreams,
      sessions: sessions.length
    };
  } catch (error) {
    return {
      configured: true,
      online: false,
      name: "Jellyfin",
      version: "unavailable",
      activeStreams: 0,
      sessions: 0,
      error: error.message
    };
  }
}

export async function fetchJellyseerrStatus(jellyseerrConfig) {
  if (!jellyseerrConfig.baseUrl || !jellyseerrConfig.apiKey) {
    return {
      configured: false,
      online: false,
      version: "unconfigured",
      movie: { pending: 0, processing: 0, approved: 0, available: 0 },
      tv: { pending: 0, processing: 0, approved: 0, available: 0 },
      error: "Jellyseerr configuration missing"
    };
  }

  const api = client(jellyseerrConfig.baseUrl, {
    "X-Api-Key": jellyseerrConfig.apiKey
  });

  try {
    const [statusRes, requestCountRes] = await Promise.all([
      api.get("/api/v1/status"),
      api.get("/api/v1/request/count")
    ]);

    const counts = requestCountRes.data || {};
    const movie = counts.movie ?? {};
    const tv = counts.tv ?? {};

    return {
      configured: true,
      online: Boolean(statusRes.data?.version),
      version: statusRes.data?.version || "unknown",
      movie: {
        pending: movie.pending || 0,
        processing: movie.processing || 0,
        approved: movie.approved || 0,
        available: movie.available || 0
      },
      tv: {
        pending: tv.pending || 0,
        processing: tv.processing || 0,
        approved: tv.approved || 0,
        available: tv.available || 0
      }
    };
  } catch (error) {
    return {
      configured: true,
      online: false,
      version: "unavailable",
      movie: { pending: 0, processing: 0, approved: 0, available: 0 },
      tv: { pending: 0, processing: 0, approved: 0, available: 0 },
      error: error.message
    };
  }
}
