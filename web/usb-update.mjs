import {
  cleanLines, parseOTA, identifyFirmware, validateManifest, verifyApp,
  matchesMarker, validatePartitions, validateOTAData, flashOptions,
} from "./update-core.mjs";

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const button = document.querySelector("#safe-update");
const status = document.querySelector("#update-status");
const progress = document.querySelector("#update-progress");
const acknowledge = document.querySelector("#update-acknowledge");
let busy = false;

function available() {
  button.disabled = busy || !acknowledge.checked || !window.isSecureContext || !navigator.serial;
}
acknowledge.addEventListener("change", available);
available();
if (!window.isSecureContext || !navigator.serial) {
  status.textContent = "USB updates require desktop Chrome or Edge over HTTPS.";
}

async function fetchFile(path, type) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path} unavailable (${response.status}).`);
  return type === "json" ? response.json() : new Uint8Array(await response.arrayBuffer());
}

// Read the console without taking ownership of the port away from this update.
async function inspectFirmware(port, version) {
  await port.open({ baudRate: 115200, bufferSize: 65536 });
  const reader = port.readable.getReader();
  const decoder = new TextDecoder();
  let text = "", readError, closing = false;
  const reading = (async () => {
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        text += decoder.decode(value, { stream: true });
        if (text.length > 98304) throw new Error("Unexpected excessive serial output.");
      }
      if (!closing) readError = new Error("Serial port disconnected.");
    } catch (error) {
      readError = error;
    } finally {
      reader.releaseLock();
    }
  })();
  async function sendOTA() {
    const writer = port.writable.getWriter();
    try { await writer.write(new TextEncoder().encode("\nota\n")); }
    finally { writer.releaseLock(); }
  }
  async function waitUntil(predicate, timeout, message, retry) {
    const end = Date.now() + timeout;
    let next = 0;
    while (Date.now() < end) {
      if (readError) throw readError;
      // Only parse complete lines; a USB packet may end halfway through a response.
      const complete = text.slice(0, text.lastIndexOf("\n") + 1);
      if (predicate(complete)) return complete;
      if (retry && Date.now() >= next) {
        await sendOTA();
        next = Date.now() + 1000;
      }
      await pause(50);
    }
    throw new Error(message);
  }
  const hasOTA = complete => cleanLines(complete).some(line => line.startsWith("v") && line.includes("running from"));
  try {
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    const before = parseOTA(await waitUntil(hasOTA, 12000,
      "No supported ota response. Wake the tank, close serial monitors and try again; nothing was flashed.", true));
    // Refuse a pending slot BEFORE resetting. Capture a fresh board banner, not stale console text.
    text = "";
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await pause(100);
    await port.setSignals({ requestToSend: false });
    await waitUntil(complete => cleanLines(complete).some(line => line.startsWith("pocket-tank ")),
      15000, "CYD boot identity was not received after reset. Nothing was flashed.");
    const identity = identifyFirmware(await waitUntil(hasOTA, 12000,
      "The rebooted firmware did not answer ota. Nothing was flashed.", true), version);
    if (identity.slot !== before.slot || identity.version !== before.version || identity.build !== before.build) {
      throw new Error("Firmware/slot changed during detection. No flash was written; finish pending updates first.");
    }
    return identity;
  } finally {
    closing = true;
    await reader.cancel().catch(() => {});
    await reading;
    await port.close();
  }
}

button.addEventListener("click", async () => {
  if (busy) return;
  busy = true;
  available();
  const factory = document.querySelector("#factory-section");
  factory.inert = true;
  progress.hidden = true;
  let port, transport, loader, identity, writing = false, verified = false;
  try {
    // Port selection must be the first await, while the click still has user activation.
    port = await navigator.serial.requestPort();
    status.textContent = "Downloading and checking the app-only release…";
    const manifest = validateManifest(await fetchFile("./update.json", "json"));
    const bytes = await fetchFile(`./${manifest.app}`, "bytes");
    await verifyApp(bytes, manifest);
    status.textContent = "Checking running/boot slots, then restarting to verify the CYD board identity…";
    identity = await inspectFirmware(port, manifest.version);
    if (!window.confirm(`Verified Pocket Tank CYD ${identity.version}, ${identity.slot}.\n\n` +
        `Update to ${manifest.version} at 0x${identity.address.toString(16)}?\n` +
        "Only the running app will be replaced. NVS, saved tanks and the partition table will not be written.\n\n" +
        "Keep USB power connected. An interrupted active-slot write may require USB recovery. Continue?")) {
      status.textContent = "Update cancelled. No flash was written.";
      return;
    }
    status.textContent = "Connecting to the bootloader on the same USB port. Do not disconnect…";
    // Pinned public APIs: ESPLoader.main/writeFlash/flashMd5sum/readFlash/after, Transport.disconnect.
    const { ESPLoader, Transport } = await import("https://esm.sh/esptool-js@0.5.4");
    transport = new Transport(port, true);
    loader = new ESPLoader({ transport, baudrate: 115200, romBaudrate: 115200,
      terminal: { clean() {}, write() {}, writeLine() {} } });
    await loader.main();
    if (loader.chip?.CHIP_NAME !== "ESP32") throw new Error("Wrong chip family. No flash was written.");
    validatePartitions(await loader.readFlash(0x8000, 0x1000));
    validateOTAData(await loader.readFlash(0xf000, 0x2000), identity);
    if (!matchesMarker(await loader.readFlash(identity.address + 0x120, 32))) {
      throw new Error("Installed app is not the verified CYD firmware. No flash was written.");
    }
    const options = flashOptions(identity, bytes, (_file, written, total) => {
      progress.value = total ? written / total : 0;
      status.textContent = `Writing app only to ${identity.slot}: ${Math.round(progress.value * 100)}%. Keep USB connected.`;
    });
    progress.hidden = false;
    writing = true;
    await loader.writeFlash(options);
    status.textContent = "Verifying the written app. Keep USB connected…";
    if ((await loader.flashMd5sum(identity.address, bytes.length)).toLowerCase() !== manifest.md5) {
      throw new Error("Flashed app checksum mismatch.");
    }
    verified = true;
    await loader.after("hard_reset");
    status.textContent = `Updated to ${manifest.version}. App verified; saved tanks/settings were not erased. Wait for the aquarium to restart.`;
  } catch (error) {
    status.textContent = `${error.name === "NotFoundError" ? "No USB port selected." : error.message} ` +
      (writing && !verified ? `The active app may be incomplete. Recovery slot: ${identity.slot} / 0x${identity.address.toString(16)}. ` +
        "Keep this page open and seek app-only USB recovery help; do NOT factory reset to recover saves." :
        verified ? "App verification succeeded; press RESET to restart if needed." :
          "Update stopped without writing flash. No factory installation was started. Press RESET if the aquarium is not running.");
  } finally {
    try {
      if (transport) await transport.disconnect();
      else if (port?.readable || port?.writable) await port.close();
    } catch (error) {
      status.textContent += ` Port cleanup failed: ${error.message}. Close this tab before reconnecting.`;
    }
    busy = false;
    factory.inert = false;
    available();
  }
});

window.addEventListener("beforeunload", event => {
  if (busy) { event.preventDefault(); event.returnValue = ""; }
});
