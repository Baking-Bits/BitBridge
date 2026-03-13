window.INSTALLER_DEVICES = [
  {
    id: "aura-cyd-2432",
    name: "Aura - ESP32-2432S028R (CYD)",
    boardHint: "2.8in ILI9341 display board",
    docsUrl: "https://makerworld.com/en/models/1382304-aura-smart-weather-forecast-display",
    variants: [
      {
        id: "stable",
        label: "Install Aura (Stable)",
        notes: "Recommended for most users.",
        manifest: "./bin/aura/manifest.json"
      },
      {
        id: "inverted",
        label: "Install Aura (Inverted Colors Fix)",
        notes: "Use this if colors are inverted on screen.",
        manifest: "./bin/aura/manifest-inverted.json"
      }
    ]
  },
  {
    id: "my-other-esp32",
    name: "My Other ESP32",
    boardHint: "Example placeholder - replace with your second board",
    docsUrl: "",
    variants: [
      {
        id: "stable",
        label: "Install Firmware",
        notes: "Replace this manifest path with your own build output.",
        manifest: "./bin/my-other-esp32/manifest.json"
      }
    ]
  }
];