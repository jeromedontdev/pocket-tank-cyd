// Fixed CYD layout; never take a flash address from a downloaded manifest or user input.
import { boardProfile, boardMarker } from "./board-profiles.mjs";
export const SLOT_SIZE = 0x1e0000;
const SLOTS = Object.freeze({ ota_0: 0x20000, ota_1: 0x200000 });
export const BOARD_MARKER = boardMarker("cyd2432s028");

function versionParts(version) {
  if (typeof version !== "string" || !/^\d{1,4}\.\d{1,4}\.\d{1,4}$/.test(version)) {
    throw new Error("Unsupported firmware version.");
  }
  return version.split(".").map(Number);
}

function compareVersions(a, b) {
  const x = versionParts(a), y = versionParts(b);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return Math.sign(x[i] - y[i]);
  return 0;
}

export function cleanLines(text) {
  return text.replace(/\x1b\[[0-9;]*m/g, "").split(/\r?\n/)
    .map(line => line.trim().replace(/^[IWE] \(\d+\) [\w-]+: /, ""));
}

export function parseOTA(text) {
  const lines = cleanLines(text).filter(line => line.startsWith("v") && line.includes("running from"));
  if (!lines.length) throw new Error("No supported ota response. No flash was written.");
  const results = lines.map(line => {
    const match = /^v(\d+\.\d+\.\d+) CYD \(build ([a-zA-Z0-9._-]{1,48})\) running from (ota_[01]), next boot (ota_[01]) \| manifest \S* \| ota check \(= CHECK FOR UPDATES\) \| ota url <url>\|default$/.exec(line);
    if (!match) throw new Error("Unknown/unsupported ota response. No flash was written.");
    if (match[3] !== match[4]) {
      throw new Error("Running and next-boot slots differ (pending update). Reboot and finish that update first.");
    }
    return { version: match[1], build: match[2], slot: match[3], address: SLOTS[match[3]] };
  });
  if (results.some(result => JSON.stringify(result) !== JSON.stringify(results[0]))) {
    throw new Error("Conflicting ota responses. No flash was written.");
  }
  return results[0];
}

export function identifyFirmware(text, targetVersion, board = "cyd2432s028") {
  boardProfile(board);
  const ota = parseOTA(text);
  const banners = cleanLines(text).filter(line => line.startsWith("pocket-tank "));
  const expected = `pocket-tank v${ota.version} CYD (build ${ota.build}) for ${board} boot`;
  if (!banners.length || banners.some(line => line !== expected &&
      line !== `${expected} (woken by button)`)) {
    throw new Error("Missing or mismatched CYD board/firmware boot identity. No flash was written.");
  }
  if (compareVersions(ota.version, "1.0.0") < 0 || compareVersions(ota.version, targetVersion) > 0) {
    throw new Error("Unsupported firmware or downgrade refused. Use the on-device Firmware Updates menu (SD Updates on older versions) or a matching newer release.");
  }
  return ota;
}

export function validateManifest(manifest, board = manifest?.board) {
  const profile = boardProfile(board);
  if (!manifest || manifest.format !== 1 || manifest.board !== board ||
      manifest.app !== profile.app ||
      !Number.isInteger(manifest.size) || manifest.size < 0x140 || manifest.size > SLOT_SIZE ||
      !/^[a-f0-9]{64}$/.test(manifest.sha256) || !/^[a-f0-9]{32}$/.test(manifest.md5)) {
    throw new Error("Invalid save-preserving update manifest.");
  }
  versionParts(manifest.version);
  return manifest;
}

export function matchesMarker(bytes, board = "cyd2432s028") {
  const marker = boardMarker(board);
  return bytes.length === marker.length &&
    bytes.every((value, index) => value === marker[index]);
}

export async function verifyApp(bytes, manifest) {
  validateManifest(manifest);
  if (bytes.length !== manifest.size || bytes[0] !== 0xe9 || bytes[12] !== 0 ||
      bytes[13] !== 0 || !matchesMarker(bytes.slice(0x120, 0x140), manifest.board)) {
    throw new Error("Downloaded app has the wrong size, chip or CYD board marker.");
  }
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  const hash = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
  if (hash !== manifest.sha256) throw new Error("App SHA-256 checksum mismatch. No flash was written.");
}

export function validatePartitions(bytes) {
  const expected = [
    [1, 2, 0x9000, 0x6000, "nvs"], [1, 0, 0xf000, 0x2000, "otadata"],
    [1, 1, 0x11000, 0x1000, "phy_init"], [0, 16, 0x20000, SLOT_SIZE, "ota_0"],
    [0, 17, 0x200000, SLOT_SIZE, "ota_1"], [1, 130, 0x3e0000, 0x20000, "storage"],
  ];
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < expected.length; i++) {
    const offset = i * 32, [type, subtype, address, size, label] = expected[i];
    if (bytes.length < offset + 32 || view.getUint16(offset, true) !== 0x50aa ||
        bytes[offset + 2] !== type || bytes[offset + 3] !== subtype ||
        view.getUint32(offset + 4, true) !== address || view.getUint32(offset + 8, true) !== size ||
        new TextDecoder().decode(bytes.slice(offset + 12, offset + 28)).replace(/\0+$/, "") !== label ||
        view.getUint32(offset + 28, true) !== 0) {
      throw new Error("Installed partition layout is unsupported. No flash was written.");
    }
  }
  // The only permitted next entry is the ESP-IDF MD5 record or erased end marker.
  if (bytes.length < 224 || ![0xebeb, 0xffff].includes(view.getUint16(192, true))) {
    throw new Error("Unexpected additional flash partitions. No flash was written.");
  }
}

export function validateOTAData(bytes, identity) {
  if (bytes.length !== 0x2000) throw new Error("Incomplete OTA selection data. No flash was written.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const records = [];
  let seen = 0;
  for (const offset of [0, 0x1000]) {
    if (bytes.slice(offset, offset + 32).every(byte => byte === 255)) continue;
    seen++;
    const sequence = view.getUint32(offset, true), state = view.getUint32(offset + 24, true);
    // ESP-IDF bootloader_common_ota_select_crc: esp_rom_crc32_le(UINT32_MAX, seq, 4).
    let crc = 0;
    for (const byte of bytes.slice(offset, offset + 4)) {
      crc ^= byte;
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    crc = (~crc) >>> 0;
    if (!sequence || sequence === 0xffffffff || crc !== view.getUint32(offset + 28, true) ||
        ![0, 1, 2, 3, 4, 0xffffffff].includes(state)) {
      throw new Error("Unknown or corrupt OTA selection record. No flash was written.");
    }
    if (state === 0 || state === 1) {
      throw new Error("OTA image is new/pending verification. Finish the existing update before USB flashing.");
    }
    if (state === 2 || state === 0xffffffff) records.push(sequence);
  }
  const selected = records.length ? (Math.max(...records) - 1) % 2 : 0;
  if ((records.length === 0 && seen !== 0) ||
      identity.slot !== `ota_${selected}`) {
    throw new Error("OTA selection disagrees with the running slot. No flash was written.");
  }
}

export function flashOptions(identity, bytes, reportProgress) {
  if (!identity || SLOTS[identity.slot] !== identity.address ||
      !Object.hasOwn(SLOTS, identity.slot) || bytes.length < 0x140 || bytes.length > SLOT_SIZE) {
    throw new Error("Unsafe app-only flash request.");
  }
  let data = "";
  for (let offset = 0; offset < bytes.length; offset += 8192) {
    data += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
  }
  return {
    fileArray: [{ data, address: identity.address }],
    flashSize: "keep", flashMode: "keep", flashFreq: "keep",
    eraseAll: false, compress: true, reportProgress,
  };
}
