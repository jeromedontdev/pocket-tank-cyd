export const BOARDS = Object.freeze({
  cyd2432s028: Object.freeze({
    name: "ESP32-2432S028 CYD", manifest: "./manifest.json",
    update: "./update.json", app: "pocket-tank-cyd-app.bin",
  }),
  elegoo28: Object.freeze({
    name: "ELEGOO ESP32-32E 2.8-inch ILI9341V", manifest: "./elegoo-manifest.json",
    update: "./elegoo-update.json", app: "pocket-tank-elegoo-app.bin",
  }),
  cydesp32profiles: Object.freeze({
    name: "CYD / ELEGOO saved hardware profiles", manifest: "./unified-manifest.json",
    update: "./unified-update.json", app: "pocket-tank-unified-app.bin",
  }),
});

export function boardProfile(identity) {
  if (!Object.hasOwn(BOARDS, identity)) throw new Error("Unsupported display board.");
  return BOARDS[identity];
}

export function boardMarker(identity) {
  boardProfile(identity);
  return new TextEncoder().encode("PTBOARD\0" + identity.padEnd(24, "\0"));
}
