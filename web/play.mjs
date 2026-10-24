const KEY = "pocket-tank-browser-v1";
const FORMAT = "pocket-tank-browser";
const MAX_BYTES = 16 * 1024 * 1024;
const $ = id => document.getElementById(id);
const canvas = $("screen");
const context = canvas.getContext("2d", {alpha: false});
let game, initialized = false, paused = false, lastStored = null, ownsLock = false;
let image, lastTime = 0, pointer = null, lastPoint = [0, 0];
let placingFood = false;
let touchHints = false, marker = null;
const panel = $("game-panel");
$("touch-hints").addEventListener("click", () => {
  touchHints = !touchHints;
  $("touch-hints").setAttribute("aria-pressed", String(touchHints));
  $("game-message").textContent = touchHints ?
    "Tap markers are on. Tap the water surface to feed; Place food lets you choose a column anywhere in the tank." : "";
});

function foodMode(enabled) {
  placingFood = enabled;
  $("feed").setAttribute("aria-pressed", String(enabled));
  $("game-message").textContent = enabled ? "Tap the tank where food should fall. Place food again cancels." : "";
}
function dropFood(x) {
  if (game._sim_feed_at(x)) {
    foodMode(false);
    $("game-message").textContent = "Food dropped from the surface at your chosen position.";
  } else $("game-message").textContent = "No food placed: the tank may already have its maximum pellets. Wait for fish to eat, or finish any open tool/setup.";
  save();
}
$("feed").addEventListener("click", () => {
  if (!initialized || paused || !game._sim_mode_chosen()) return;
  if (!placingFood) game._sim_action(0);
  foodMode(!placingFood);
});
$("fullscreen").addEventListener("click", async () => {
  try {
    releasePointer(true);
    if (document.fullscreenElement === panel) await document.exitFullscreen();
    else {
      if (!panel.requestFullscreen) throw Error("Fullscreen is not supported by this browser.");
      await panel.requestFullscreen();
    }
  } catch (error) { $("game-message").textContent = "Could not change fullscreen: " + error.message; }
});
document.addEventListener("fullscreenchange", () => {
  const active = document.fullscreenElement === panel;
  $("fullscreen").textContent = active ? "Exit fullscreen" : "Fullscreen";
  $("fullscreen").setAttribute("aria-pressed", String(active));
});
let createGame, assetBase, iconBytes;

async function restoreInstance(archive, active = false) {
  if (!createGame || !iconBytes) throw Error("Game assets are not ready. Retry startup before importing.");
  let aborted = null;
  const instance = await createGame({locateFile: name => new URL(name, assetBase).href,
    printErr: message => console.error(message),
    onAbort: message => {
      aborted = Error("Game stopped: " + message);
      if (active) { paused = true; fail(aborted); }
    }});
  instance.FS.mkdir("/save");
  if (archive) {
    for (const file of archive.files) {
      const path = "/save/" + file.path;
      instance.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
      const binary = atob(file.data);
      instance.FS.writeFile(path, Uint8Array.from(binary, char => char.charCodeAt(0)));
    }
  }
  instance.FS.chdir("/save");
  const pointer = instance._malloc(iconBytes.length);
  if (!pointer) throw Error("Could not allocate game artwork.");
  try {
    instance.HEAPU8.set(iconBytes, pointer);
    if (!instance._sim_init(pointer, iconBytes.length))
      throw Error(instance.UTF8ToString(instance._sim_error()) || "Game initialization failed.");
    if (aborted) throw aborted;
  } finally { instance._free(pointer); }
  return instance;
}

function status(text, error = false) {
  $("status").textContent = text;
  $("status").classList.toggle("error", error);
}
function fail(error) {
  status(String(error.message || error) + " Export your current save before resetting or closing this tab.", true);
}
function decodeArchive(text) {
  if (typeof text !== "string" || text.length > MAX_BYTES * 2) throw Error("Browser save is too large.");
  const archive = JSON.parse(text);
  if (archive.format !== FORMAT || archive.schema !== 1 || !Array.isArray(archive.files) || archive.files.length > 2048)
    throw Error("Not a supported complete browser save.");
  const seen = new Set();
  let total = 0;
  for (const file of archive.files) {
    if (!file || typeof file.path !== "string" || !/^[a-zA-Z0-9_.-]+(?:\/[a-zA-Z0-9_.-]+)*$/.test(file.path) ||
        file.path.split("/").some(p => p === "." || p === "..") || /\.(next|new)$/.test(file.path) ||
        typeof file.data !== "string" || seen.has(file.path)) throw Error("Invalid or duplicate file in browser save.");
    seen.add(file.path);
    const bytes = atob(file.data);
    total += bytes.length;
    if (total > MAX_BYTES) throw Error("Browser save exceeds the supported size.");
  }
  return archive;
}
function encode(bytes) {
  let binary = "";
  for (let i = 0; i < bytes.length; i += 8192) binary += String.fromCharCode(...bytes.subarray(i, i + 8192));
  return btoa(binary);
}
function archiveFiles() {
  const files = [];
  function walk(path) {
    for (const name of game.FS.readdir(path).sort()) {
      if (name === "." || name === "..") continue;
      const full = path + "/" + name;
      const mode = game.FS.stat(full).mode;
      if (game.FS.isDir(mode)) walk(full);
      else if (game.FS.isFile(mode) && !/\.(next|new)$/.test(name))
        files.push({path: full.slice(6), data: encode(game.FS.readFile(full))});
    }
  }
  walk("/save");
  return JSON.stringify({format: FORMAT, schema: 1, files});
}
function save() {
  if (!initialized || paused || !ownsLock) return false;
  try {
    if (localStorage.getItem(KEY) !== lastStored) {
      paused = true;
      throw Error("Save changed in another tab. Play paused to protect both copies. Export this tab, then reload.");
    }
    // C commits its profiles and Bank synchronously; capture only after it returns.
    if (!game._sim_save()) throw Error("The game could not commit its save files. Retry or export to recover.");
    const next = archiveFiles();
    localStorage.setItem(KEY, next); // One synchronous atomic replacement, including on pagehide.
    lastStored = next;
    $("save-detail").textContent = "Saved locally at " + new Date().toLocaleTimeString() + ". No device synchronization.";
    status("Playing · saved in this browser");
    return true;
  } catch (error) {
    $("save-detail").textContent = "SAVE FAILED — progress is still in this tab. Retry or export now.";
    fail(error);
    return false;
  }
}
function refreshControls() {
  const setup = !game._sim_mode_chosen();
  document.querySelectorAll("[data-action]").forEach(button => { button.disabled = setup || paused; });
  $("save").disabled = paused;
  $("feed").disabled = setup || paused;
}
function point(event) {
  const rect = canvas.getBoundingClientRect();
  // object-fit letterboxes when a short viewport caps the canvas height.
  const scale = Math.min(rect.width / canvas.width, rect.height / canvas.height);
  const left = rect.left + (rect.width - canvas.width * scale) / 2;
  const top = rect.top + (rect.height - canvas.height * scale) / 2;
  const x = (event.clientX - left) / scale, y = (event.clientY - top) / scale;
  return x < 0 || y < 0 || x >= canvas.width || y >= canvas.height ? null : [x, y];
}
function releasePointer(cancelled = false) {
  if (pointer === null || !game) return;
  game._sim_touch(...(cancelled ? [-1000, -1000] : lastPoint), 0);
  pointer = null;
  if (placingFood && !cancelled) {
    dropFood(lastPoint[0]);
  }
  refreshControls();
  save();
}
canvas.addEventListener("pointerdown", event => {
  if (!initialized || paused || pointer !== null || (event.pointerType === "mouse" && event.button !== 0)) return;
  const target = point(event);
  if (!target) return;
  event.preventDefault();
  pointer = event.pointerId;
  canvas.setPointerCapture(pointer);
  lastPoint = target;
  marker = {x: target[0], y: target[1], until: performance.now() + 800};
  if (!placingFood) game._sim_touch(...lastPoint, 1);
});
canvas.addEventListener("pointermove", event => {
  if (event.pointerId !== pointer) return;
  event.preventDefault();
  const target = point(event);
  if (!target) return;
  lastPoint = target;
  marker = {x: target[0], y: target[1], until: performance.now() + 800};
  if (!placingFood) game._sim_touch(...lastPoint, 1);
});
canvas.addEventListener("pointerup", event => {
  if (event.pointerId !== pointer) return;
  event.preventDefault();
  const target = point(event);
  if (target) lastPoint = target;
  releasePointer(!target);
});
canvas.addEventListener("pointercancel", () => releasePointer(true));
canvas.addEventListener("lostpointercapture", () => releasePointer(true));
canvas.addEventListener("contextmenu", event => event.preventDefault());
document.querySelectorAll("[data-action]").forEach(button => {
  button.addEventListener("click", () => {
    if (!initialized || paused) return;
    foodMode(false);
    game._sim_action(Number(button.dataset.action));
    save();
  });
});
function frame(now) {
  if (!initialized || paused) return;
  try {
    const dt = lastTime ? Math.min(.1, (now - lastTime) / 1000) : .04;
    lastTime = now;
    const pixels = game._sim_frame(dt) >>> 1;
    const width = game._sim_width(), height = game._sim_height();
    if (!image || width !== canvas.width || height !== canvas.height) {
      canvas.width = width; canvas.height = height;
      image = context.createImageData(width, height);
    }
    const brightness = game._sim_display_brightness();
    for (let i = 0; i < width * height; i++) {
      const value = game.HEAPU16[pixels + i], j = i * 4;
      image.data[j] = Math.floor(((value >>> 11) & 31) * brightness / 31);
      image.data[j + 1] = Math.floor(((value >>> 5) & 63) * brightness / 63);
      image.data[j + 2] = Math.floor((value & 31) * brightness / 31);
      image.data[j + 3] = 255;
    }
    context.putImageData(image, 0, 0);
    if (touchHints && marker && marker.until > now) {
      context.strokeStyle = "#ffd982";
      context.lineWidth = 1;
      context.beginPath();
      context.arc(marker.x, marker.y, 8, 0, Math.PI * 2);
      context.moveTo(marker.x - 12, marker.y); context.lineTo(marker.x + 12, marker.y);
      context.moveTo(marker.x, marker.y - 12); context.lineTo(marker.x, marker.y + 12);
      context.stroke();
    }
    refreshControls();
    requestAnimationFrame(frame);
  } catch (error) { paused = true; fail(error); }
}
$("save").addEventListener("click", save);
$("reload").addEventListener("click", () => location.reload());
function download(text) {
  const url = URL.createObjectURL(new Blob([text], {type: "application/json"}));
  const link = document.createElement("a");
  link.href = url; link.download = "pocket-tank-browser-" + new Date().toISOString().slice(0, 10) + ".json";
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$("export").addEventListener("click", () => {
  try {
    // If a native commit failed, still offer the last coherent in-memory files.
    // Also works when storage was denied or the on-disk archive was corrupt.
    if (initialized) {
      const committed = !!game._sim_save();
      download(archiveFiles());
      if (!committed) fail(Error("Exported the last committed files; the latest game commit failed."));
    } else {
      const stored = localStorage.getItem(KEY);
      if (!stored) throw Error("No browser save exists yet.");
      download(stored);
    }
  } catch (error) { fail(error); }
});
$("import").addEventListener("click", () => $("file").click());
$("file").addEventListener("change", async event => {
  const file = event.target.files[0];
  event.target.value = "";
  if (!file) return;
  try {
    if (!ownsLock) throw Error("Close the other game tab and reload before importing.");
    if (file.size > MAX_BYTES * 2) throw Error("Browser save is too large.");
    const text = await file.text();
    const archive = decodeArchive(text);
    // Separate WASM memory and filesystem: never mutate the running game or
    // durable key until the actual native profile/preferences load succeeds.
    await restoreInstance(archive);
    if (!confirm("Replace ALL browser profiles, Bank and preferences with this archive? Export first if you want to keep the current game. The device is unaffected.")) return;
    localStorage.setItem(KEY, text);
    paused = true; // Do not overwrite the imported archive during navigation.
    location.reload();
  } catch (error) { fail(error); }
});
$("reset").addEventListener("click", () => {
  try {
    if (!ownsLock) throw Error("Close the other game tab and reload before resetting.");
    if (!confirm("Permanently erase ALL browser profiles, Bank and preferences? Export first to keep a backup. Your device is unaffected.")) return;
    localStorage.removeItem(KEY);
    paused = true;
    location.reload();
  } catch (error) { fail(error); }
});
document.addEventListener("visibilitychange", () => {
  if (document.hidden) { releasePointer(true); save(); }
  lastTime = 0;
});
addEventListener("pagehide", () => { releasePointer(true); save(); });
addEventListener("storage", event => {
  if (event.key === KEY && initialized && event.newValue !== lastStored) {
    paused = true; refreshControls();
    fail(Error("Browser save changed elsewhere. This tab is paused; export or reload."));
  }
});

async function start() {
  try {
    if (!navigator.locks) throw Error("This browser needs Web Locks support and a secure HTTPS (or localhost) connection.");
    // Keep the lock for this document's lifetime, including startup recovery.
    await new Promise((resolve, reject) => {
      navigator.locks.request("pocket-tank-browser-save", {ifAvailable: true}, async lock => {
        if (!lock) { reject(Error("Another tab is playing. Close it, then reload this page.")); return; }
        ownsLock = true;
        resolve();
        await new Promise(() => {}); // Released by the browser when this document closes.
      }).catch(reject);
    });
    lastStored = localStorage.getItem(KEY);
    const archive = lastStored === null ? null : decodeArchive(lastStored);
    // Fail before initialization if storage is unavailable, rather than pretending to save.
    const probe = KEY + "-probe";
    localStorage.setItem(probe, "1"); localStorage.removeItem(probe);
    const buildResponse = await fetch("play-build.json", {cache: "no-store"});
    if (!buildResponse.ok) throw Error("Game build is missing. Please reload or try again after deployment.");
    const build = await buildResponse.json();
    if (!/^game-[a-f0-9]{16}$/.test(build.assets)) throw Error("Invalid game build manifest.");
    $("build-info").textContent = (build.release ? "Released game " + build.release : "Development game " + build.version) +
      " · source " + build.commit.slice(0, 7) + " · WebAssembly browser simulation";
    assetBase = new URL(build.assets + "/", location.href);
    ({default: createGame} = await import(new URL("tank.mjs", assetBase)));
    const response = await fetch(new URL("icons.bin", assetBase));
    if (!response.ok) throw Error("Could not load game artwork.");
    iconBytes = new Uint8Array(await response.arrayBuffer());
    if (!iconBytes.length) throw Error("Game artwork is empty.");
    game = await restoreInstance(archive, true);
    initialized = true;
    $("save").disabled = false;
    save(); // Explicit initial commit; storage errors remain visible while playing.
    requestAnimationFrame(frame);
    setInterval(() => { if (!document.hidden) save(); }, 5000);
  } catch (error) {
    $("save-detail").textContent = "Startup failed. Existing saved data has not been replaced. Export, reset, or retry.";
    fail(error);
  }
}
start();
export {game};
