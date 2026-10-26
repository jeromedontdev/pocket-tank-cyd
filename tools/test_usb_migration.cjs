/* Actual installer handlers/downloads with a deterministic serial/bootloader device. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");
const { chromium } = require("../.browser-tools/node_modules/playwright");
const root = path.resolve(process.env.POCKET_TANK_WEB_ROOT || path.join(__dirname, "..", "_site"));

async function main() {
  const server = http.createServer((req, res) => {
    const name = path.join(root, decodeURIComponent(new URL(req.url, "http://localhost").pathname));
    if (!name.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
    fs.readFile(name, (error, data) => {
      res.writeHead(error ? 404 : 200, { "Content-Type": ({
        ".html": "text/html", ".mjs": "text/javascript", ".json": "application/json",
      })[path.extname(name)] || "application/octet-stream" });
      res.end(error ? "Not found" : data);
    });
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true,
      ...(process.argv.includes("--edge") ? { channel: "msedge" } : {}) });
    for (const hardware of ["cyd2432s028", "elegoo28"]) {
      for (const source of ["legacy", "unified", "wrong-profile", "pending"]) {
        const context = await browser.newContext();
        const page = await context.newPage();
        const errors = [];
        page.on("pageerror", error => errors.push(error.message));
        const update = JSON.parse(fs.readFileSync(path.join(root,
          hardware === "elegoo28" ? "elegoo-update.json" : "update.json")));
        assert.equal(update.board, "cydesp32profiles");
        await page.addInitScript(({ hardware, source, update }) => {
          const other = hardware === "elegoo28" ? "cyd2432s028" : "elegoo28";
          const device = window.device = {
            writes: [], provisioned: [], flashed: false,
            profile: source === "wrong-profile" ? other : hardware,
            board: source === "legacy" || source === "pending" ? hardware : "cydesp32profiles",
            waiting: false, requests: 0,
          };
          let controller;
          const emit = text => controller.enqueue(new TextEncoder().encode(text + "\n"));
          const banner = () => emit(`I (50) main: pocket-tank v${update.version} CYD (build test) for ${device.board} boot`);
          const ota = () => emit(`I (100) director: v${update.version} CYD (build test) running from ota_0, next boot ${source === "pending" ? "ota_1" : "ota_0"} | manifest https://example.test/ota.json | ota check (= CHECK FOR UPDATES) | ota url <url>|default`);
          const port = {
            async open() {
              this.readable = new ReadableStream({ start(c) { controller = c; } });
              this.writable = new WritableStream({ write(value) {
                const text = new TextDecoder().decode(value);
                for (const line of text.split("\n").filter(Boolean)) {
                  if (line === "ota" && !device.waiting) ota();
                  if (line === "hardware" && !device.waiting && device.board === "cydesp32profiles")
                    emit(`I (1234) director: hardware profile ${device.profile} | image cydesp32profiles`);
                  if (line === "hardware cyd" || line === "hardware elegoo") {
                    assertWaiting();
                    device.profile = line === "hardware cyd" ? "cyd2432s028" : "elegoo28";
                    device.provisioned.push(device.profile);
                    device.waiting = false;
                    banner();
                  }
                }
              } });
            },
            async setSignals(signals) {
              if (signals.requestToSend === false) {
                if (device.flashed && device.waiting)
                  emit("W (100) hardware: profile required: send 'hardware cyd' or 'hardware elegoo' over USB serial");
                else banner();
              }
            },
            async close() {},
          };
          function assertWaiting() {
            if (!device.waiting) throw new Error("Unexpected profile mutation on an already-configured device");
          }
          device.port = port;
          device.update = update;
          Object.defineProperty(navigator, "serial", { value: {
            requestPort: async () => { device.requests++; return port; },
          } });
        }, { hardware, source, update });
        await page.route("https://unpkg.com/**", route => route.fulfill({
          contentType: "text/javascript",
          body: 'customElements.define("esp-web-install-button", class extends HTMLElement {});',
        }));
        await page.route("https://esm.sh/esptool-js@0.5.4", route => route.fulfill({
          contentType: "text/javascript",
          body: `
            export class Transport { async disconnect() {} }
            export class ESPLoader {
              constructor() { this.chip = { CHIP_NAME: "ESP32" }; }
              async main() {}
              async readFlash(address, size) {
                const bytes = new Uint8Array(size).fill(255);
                const view = new DataView(bytes.buffer);
                if (address === 0x8000) {
                  const layout = [[1,2,0x9000,0x6000,"nvs"],[1,0,0xf000,0x2000,"otadata"],
                    [1,1,0x11000,0x1000,"phy_init"],[0,16,0x20000,0x1e0000,"ota_0"],
                    [0,17,0x200000,0x1e0000,"ota_1"],[1,130,0x3e0000,0x20000,"storage"]];
                  layout.forEach(([type,subtype,offset,length,label],i) => {
                    const o=i*32; bytes.fill(0,o,o+32); view.setUint16(o,0x50aa,true);
                    bytes[o+2]=type; bytes[o+3]=subtype; view.setUint32(o+4,offset,true);
                    view.setUint32(o+8,length,true); bytes.set(new TextEncoder().encode(label),o+12);
                  });
                } else if (address === 0x20120) {
                  bytes.set(new TextEncoder().encode("PTBOARD\\0"+window.device.board.padEnd(24,"\\0")));
                } else if (address !== 0xf000) throw new Error("Unsafe read address");
                return bytes;
              }
              async writeFlash(options) {
                if (options.eraseAll || options.fileArray.length !== 1 ||
                    options.fileArray[0].address !== 0x20000) throw new Error("Unsafe flash");
                window.device.writes.push(options.fileArray[0].address);
                const image = options.fileArray[0].data;
                if (image.length !== window.device.update.size) throw new Error("Incomplete app");
                options.reportProgress(0,image.length,image.length);
              }
              async flashMd5sum() { return window.device.update.md5; }
              async after() {
                window.device.waiting = window.device.board !== "cydesp32profiles";
                window.device.flashed = true;
                window.device.board = "cydesp32profiles";
              }
            }`,
        }));
        page.on("dialog", dialog => dialog.accept());
        await page.goto(`http://127.0.0.1:${server.address().port}/flash.html`);
        await page.locator("#board").selectOption(hardware);
        await page.waitForFunction(() => document.querySelector("#status").textContent.startsWith("Ready") &&
          !document.querySelector("#board").disabled);
        await page.locator("#update-acknowledge").check();
        await page.locator("#safe-update").click();
        await page.waitForFunction(() => !document.querySelector("#board").disabled &&
          /Updated to|Update stopped/.test(document.querySelector("#update-status").textContent), { timeout: 30000 });
        const result = await page.evaluate(() => ({
          text: document.querySelector("#update-status").textContent,
          writes: device.writes, provisioned: device.provisioned, requests: device.requests,
        }));
        assert.equal(result.requests, 1);
        if (source === "wrong-profile" || source === "pending") {
          assert.match(result.text, /Update stopped/);
          assert.deepEqual(result.writes, []);
          assert.deepEqual(result.provisioned, []);
        } else {
          assert.match(result.text, /Updated to/);
          assert.deepEqual(result.writes, [0x20000]);
          assert.deepEqual(result.provisioned, source === "legacy" ? [hardware] : []);
        }
        assert.deepEqual(errors, []);
        await context.close();
      }
    }
    console.log("PASS: actual web updater migrates both legacy boards, retains unified profiles, refuses mismatches/pending slots, and never factory flashes");
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
