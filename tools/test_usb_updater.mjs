// Runs without dependencies in Node 20+ or a browser: await (await import(...)).runTests().
import {
  BOARD_MARKER, SLOT_SIZE, parseOTA, identifyFirmware, validateManifest,
  verifyApp, validatePartitions, validateOTAData, flashOptions, identifyForHardware,
} from "../web/update-core.mjs";
import { boardMarker, boardProfile } from "../web/board-profiles.mjs";

export async function runTests() {
  let count = 0;
  function assert(ok, message = "Assertion failed") { count++; if (!ok) throw new Error(message); }
  function refuses(action) {
    let refused = false;
    try { action(); } catch { refused = true; }
    assert(refused, "Unsafe input was accepted");
  }
  async function rejects(action) {
    let refused = false;
    try { await action(); } catch { refused = true; }
    assert(refused, "Corrupted download was accepted");
  }
  const ota = (slot = "ota_0", boot = slot, version = "1.0.0", stage = "CYD") =>
    `I (1200) director: v${version} ${stage} (build abc123-dirty) running from ${slot}, next boot ${boot} | manifest https://example.test/manifest.json | ota check (= CHECK FOR UPDATES) | ota url <url>|default\n`;
  const banner = (version = "1.0.0", board = "cyd2432s028") =>
    `\x1b[0;32mI (50) main: pocket-tank v${version} CYD (build abc123-dirty) for ${board} boot\x1b[0m\n`;
  const realOTA = "I (15309) director: v1.1.0 CYD (build 8a94798-dirty) running from ota_1, next boot ota_1 | manifest  | ota check (= CHECK FOR UPDATES) | ota url <url>|default";
  const realIdentity = parseOTA(realOTA);
  assert(realIdentity.version === "1.1.0" && realIdentity.build === "8a94798-dirty");
  assert(realIdentity.slot === "ota_1" && realIdentity.address === 0x200000);
  const realBanner = "I (50) main: pocket-tank v1.1.0 CYD (build 8a94798-dirty) for cyd2432s028 boot\n";
  assert(identifyFirmware(realBanner + realOTA, "1.1.0").address === 0x200000);
  refuses(() => parseOTA(realOTA.replace("next boot ota_1", "next boot ota_0")));
  refuses(() => parseOTA(realOTA.replace("manifest  |", "manifest |")));
  refuses(() => parseOTA(realOTA.replace("manifest  |", "manifest   |")));
  refuses(() => parseOTA(realOTA.replace("| ota url <url>|default", "")));
  for (const [slot, address] of [["ota_0", 0x20000], ["ota_1", 0x200000]]) {
    const identity = identifyFirmware(banner() + ota(slot), "1.0.1");
    assert(identity.address === address);
    const options = flashOptions(identity, new Uint8Array(0x140), () => {});
    assert(options.eraseAll === false);
    assert(options.fileArray.length === 1 && options.fileArray[0].address === address);
    assert(options.flashSize === "keep" && options.flashMode === "keep" && options.flashFreq === "keep");
    assert(options.fileArray[0].data.length === 0x140);
  }
  refuses(() => parseOTA(ota("ota_0", "ota_1")));
  refuses(() => parseOTA(ota("ota_1", "ota_0")));
  refuses(() => parseOTA(ota("factory")));
  refuses(() => parseOTA(ota("ota_2")));
  refuses(() => parseOTA(ota().replace("next boot ota_0", "next boot ?")));
  refuses(() => parseOTA(ota().replace("next boot ota_0", "next boot pending")));
  refuses(() => parseOTA("running from ota_0, next boot ota_0\n"));
  refuses(() => parseOTA(ota("ota_0", "ota_0", "1.0.0", "upstream")));
  refuses(() => parseOTA(ota().replace("| ota url <url>|default", "")));
  refuses(() => parseOTA(ota() + ota("ota_1")));
  assert(parseOTA(ota() + ota()).address === 0x20000);
  assert(parseOTA("I (20) update: update mode: running from ota_0 | heap 200KB\n" + ota()).slot === "ota_0");
  refuses(() => identifyFirmware(ota(), "1.0.1"));
  refuses(() => identifyFirmware(banner("1.0.0", "otherboard") + ota(), "1.0.1"));
  refuses(() => identifyFirmware(banner("1.0.1") + ota(), "1.0.1"));
  refuses(() => identifyFirmware(banner().replace("abc123-dirty", "def456") + ota(), "1.0.1"));
  refuses(() => identifyFirmware(banner("0.3.8") + ota("ota_0", "ota_0", "0.3.8"), "1.0.1"));
  refuses(() => identifyFirmware(banner("1.0.2") + ota("ota_0", "ota_0", "1.0.2"), "1.0.1"));
  refuses(() => identifyFirmware(banner() + ota(), "not-a-version"));
  refuses(() => flashOptions({ slot: "ota_0", address: 0 }, new Uint8Array(0x140)));
  refuses(() => flashOptions({ slot: "ota_1", address: 0x20000 }, new Uint8Array(0x140)));
  refuses(() => flashOptions({ slot: "unknown", address: undefined }, new Uint8Array(0x140)));
  refuses(() => flashOptions({ slot: "ota_0", address: 0x20000 }, new Uint8Array(SLOT_SIZE + 1)));
  refuses(() => flashOptions({ slot: "ota_0", address: 0x20000 }, new Uint8Array(0x100)));
  const image = new Uint8Array(0x200);
  image[0] = 0xe9;
  image.set(BOARD_MARKER, 0x120);
  assert(BOARD_MARKER.length === 32);
  const hex = bytes => Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, "0")).join("");
  const manifest = { format: 1, board: "cyd2432s028", version: "1.0.1",
    app: "pocket-tank-cyd-app.bin", size: image.length,
    sha256: hex(await crypto.subtle.digest("SHA-256", image)), md5: "0".repeat(32) };
  validateManifest(manifest);
  await verifyApp(image, manifest);
  const elegoo = image.slice();
  elegoo.set(boardMarker("elegoo28"), 0x120);
  const elegooManifest = { ...manifest, board: "elegoo28", app: "pocket-tank-elegoo-app.bin",
    sha256: hex(await crypto.subtle.digest("SHA-256", elegoo)) };
  validateManifest(elegooManifest, "elegoo28");
  assert(identifyFirmware(banner("1.0.0", "elegoo28") + ota(), "1.0.1", "elegoo28").slot === "ota_0");
  await verifyApp(elegoo, elegooManifest);
  const unified = image.slice();
  unified.set(boardMarker("cydesp32profiles"), 0x120);
  for (const hardware of ["cyd2432s028", "elegoo28"]) {
    const shared = { ...manifest, board: "cydesp32profiles", hardware,
      app: "pocket-tank-unified-app.bin",
      sha256: hex(await crypto.subtle.digest("SHA-256", unified)) };
    validateManifest(shared, hardware);
    await verifyApp(unified, shared);
    const profile = `I (1234) director: hardware profile ${hardware} | image cydesp32profiles\n`;
    const boot = banner("1.0.0", "cydesp32profiles") + ota();
    assert(identifyForHardware(boot + profile, "1.0.1", hardware).board === "cydesp32profiles");
    assert(identifyForHardware(banner("1.0.0", hardware) + ota(), "1.0.1", hardware).board === hardware);
    refuses(() => identifyForHardware(boot, "1.0.1", hardware));
    refuses(() => identifyForHardware(boot + profile, "1.0.1",
      hardware === "elegoo28" ? "cyd2432s028" : "elegoo28"));
    refuses(() => validateManifest(shared, hardware === "elegoo28" ? "cyd2432s028" : "elegoo28"));
  }
  refuses(() => identifyFirmware(banner() + ota(), "1.0.1", "elegoo28"));
  refuses(() => identifyFirmware(banner("1.0.0", "elegoo28") + ota(), "1.0.1"));
  refuses(() => validateManifest(manifest, "elegoo28"));
  refuses(() => validateManifest(elegooManifest, "cyd2432s028"));
  refuses(() => validateManifest({ ...manifest, board: "elegoo28" }));
  refuses(() => boardProfile("unknown"));
  refuses(() => boardProfile("__proto__"));
  await rejects(() => verifyApp(image, { ...elegooManifest, sha256: manifest.sha256 }));
  await rejects(() => verifyApp(elegoo, { ...manifest, sha256: elegooManifest.sha256 }));
  for (const change of [
    { format: 2 }, { board: "other" }, { app: "pocket-tank-cyd-factory.bin" },
    { app: "https://example.test/image.bin" }, { size: 0 }, { size: SLOT_SIZE + 1 },
    { size: 512.5 }, { sha256: "wrong" }, { md5: "wrong" }, { version: "1.0.1-dev" },
  ]) refuses(() => validateManifest({ ...manifest, ...change }));
  await rejects(() => verifyApp(image, { ...manifest, sha256: "0".repeat(64) }));
  for (const offset of [0, 12, 13, 0x128, 0x150]) {
    const bad = image.slice(); bad[offset] ^= 1;
    await rejects(() => verifyApp(bad, manifest));
  }
  const partitions = new Uint8Array(4096).fill(255);
  const view = new DataView(partitions.buffer);
  const layout = [
    [1, 2, 0x9000, 0x6000, "nvs"], [1, 0, 0xf000, 0x2000, "otadata"],
    [1, 1, 0x11000, 0x1000, "phy_init"], [0, 16, 0x20000, SLOT_SIZE, "ota_0"],
    [0, 17, 0x200000, SLOT_SIZE, "ota_1"], [1, 130, 0x3e0000, 0x20000, "storage"],
  ];
  layout.forEach(([type, subtype, address, size, label], i) => {
    const o = i * 32;
    partitions.fill(0, o, o + 32);
    view.setUint16(o, 0x50aa, true);
    partitions[o + 2] = type; partitions[o + 3] = subtype;
    view.setUint32(o + 4, address, true); view.setUint32(o + 8, size, true);
    partitions.set(new TextEncoder().encode(label), o + 12);
  });
  validatePartitions(partitions);
  view.setUint16(192, 0xebeb, true);
  validatePartitions(partitions);
  for (const offset of [0, 2, 3, 4, 8, 12, 28, 96 + 4, 128 + 8, 192]) {
    const bad = partitions.slice(); bad[offset] ^= 1;
    refuses(() => validatePartitions(bad));
  }
  refuses(() => validatePartitions(new Uint8Array(64)));
  const otaData = new Uint8Array(0x2000).fill(255);
  validateOTAData(otaData, { slot: "ota_0" });
  refuses(() => validateOTAData(otaData, { slot: "ota_1" }));
  const otaView = new DataView(otaData.buffer);
  otaView.setUint32(0, 1, true);
  otaView.setUint32(24, 2, true);
  otaView.setUint32(28, 0x4743989a, true);
  validateOTAData(otaData, { slot: "ota_0" });
  for (const state of [0, 1, 9]) {
    otaView.setUint32(24, state, true);
    refuses(() => validateOTAData(otaData, { slot: "ota_0" }));
  }
  otaView.setUint32(24, 2, true);
  otaView.setUint32(0x1000, 2, true);
  otaView.setUint32(0x1000 + 24, 2, true);
  otaView.setUint32(0x1000 + 28, 0x55f63774, true);
  validateOTAData(otaData, { slot: "ota_1" });
  refuses(() => validateOTAData(otaData, { slot: "ota_0" }));
  otaView.setUint32(0x1000 + 28, 0, true);
  refuses(() => validateOTAData(otaData, { slot: "ota_1" }));
  return `${count} USB updater assertions passed`;
}
