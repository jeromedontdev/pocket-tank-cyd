/* Run after build_browser.py. npm install --prefix .browser-tools --no-save playwright@1.56.1
 * npx --prefix .browser-tools playwright install chromium
 * node tools/test_browser.cjs [--edge] [--url http://127.0.0.1:8766/play.html]
 * Without --url this test serves _site itself. All test saves are synthetic.
 */
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require("../.browser-tools/node_modules/playwright");
const root = path.resolve(__dirname, "..", "_site");
const KEY = "pocket-tank-browser-v1";
let server;
const urlIndex = process.argv.indexOf("--url");

async function main() {
  let url = urlIndex >= 0 ? process.argv[urlIndex + 1] : null;
  if (!url) {
    server = http.createServer((req, res) => {
      const name = path.join(root, decodeURIComponent(new URL(req.url, "http://localhost").pathname));
      if (!name.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
      const types = {".html": "text/html", ".css": "text/css", ".mjs": "text/javascript",
        ".wasm": "application/wasm", ".json": "application/json"};
      fs.readFile(name, (error, data) => {
        res.writeHead(error ? 404 : 200, {"Content-Type": types[path.extname(name)] || "application/octet-stream",
          "Cache-Control": "no-store"});
        res.end(error ? "Not found" : data);
      });
    });
    await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
    url = `http://127.0.0.1:${server.address().port}/play.html`;
  }
  const browser = await chromium.launch({headless: true,
    ...(process.argv.includes("--edge") ? {channel: "msedge"} : {})});
  try {
    const context = await browser.newContext({acceptDownloads: true, viewport: {width: 1100, height: 1000}});
    const page = await context.newPage();
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    async function ready(target = page) {
      await target.waitForFunction(() => document.querySelector("#status").textContent.startsWith("Playing") ||
        document.querySelector("#status").classList.contains("error"));
      assert.ok((await target.locator("#status").textContent()).startsWith("Playing"),
        await target.locator("#status").textContent());
    }
    async function native(expression) {
      return page.evaluate(async expression => {
        const src = document.querySelector('script[type="module"]').src;
        const {game} = await import(src);
        return Function("game", expression)(game);
      }, expression);
    }
    async function snapshot() {
      return native(`const p = game._sim_backup(); try {return JSON.parse(game.UTF8ToString(p));} finally {game._sim_free(p);}`);
    }
    async function tap(x, y) {
      const box = await page.locator("#screen").boundingBox();
      await page.mouse.click(box.x + x / 448 * box.width, box.y + y / 368 * box.height);
    }
    await page.goto(url); await ready();
    assert.equal(await native("return game._sim_mode_chosen()"), 0);
    assert.equal(await page.locator('[data-action="0"]').isDisabled(), true);
    await tap(200, 98); await tap(200, 300);
    assert.equal(await native("return game._sim_mode_chosen()"), 1);
    await page.locator('[data-action="0"]').click();
    await page.waitForTimeout(150);
    assert.equal(await native("return game._sim_page()"), 0);
    await page.locator('[data-action="2"]').click();
    await tap(330, 260); await tap(200, 120);
    assert.equal(await native("return game._sim_page()"), 2, "Browser must stay in settings");
    assert.equal(await native("return game._sim_network_page()"), 0, "No embedded credential/update flow");
    assert.equal(await native("return game.UTF8ToString(game._sim_error())"), "FIRMWARE UPDATES ARE ON THE CYD");
    await page.locator('[data-action="0"]').click();
    await page.waitForTimeout(150);
    const colors = await page.locator("#screen").evaluate(canvas => {
      const bytes = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
      const colors = new Set();
      for (let i = 0; i < bytes.length; i += 4) {
        if (bytes[i + 3] !== 255) throw Error("Invalid framebuffer alpha");
        colors.add(`${bytes[i]},${bytes[i + 1]},${bytes[i + 2]}`);
      }
      return colors.size;
    });
    assert.ok(colors > 50, "Actual colorful renderer must run");
    for (const [action, expected] of [[1, 1], [2, 2], [3, 3], [7, 9], [0, 0]]) {
      await page.locator(`[data-action="${action}"]`).click();
      assert.equal(await native("return game._sim_page()"), expected);
    }
    // Launch a real mini-game from the visible Life hub, then pop rendered bubbles.
    await page.locator('[data-action="7"]').click(); await tap(100, 260);
    assert.equal(await native("return game._sim_page()"), 8);
    await tap(110, 140);
    await page.waitForTimeout(800);
    const bubble = await native(`
      const width = game._sim_width(), height = game._sim_height();
      const pixels = game._sim_frame(0) >>> 1;
      for (let y = 110; y < 320; y++) for (let x = 32; x < 424; x++) {
        const i = Math.round(y * height / 368) * width + Math.round(x * width / 448);
        if (game.HEAPU16[pixels + i] === 65535) return [x + 6, y + 10];
      }
      return null;`);
    assert.ok(bubble, "Bubble Pop must render a playable bubble");
    await tap(...bubble);
    await page.locator('[data-action="0"]').click();
    await page.locator('[data-action="2"]').click();
    await tap(100, 100); await tap(200, 192);
    assert.equal(await native("return game._sim_scene_detail()"), 1);
    await page.locator('[data-action="2"]').click();
    await tap(330, 100); await tap(220, 69);
    assert.equal((await snapshot()).tank.clock_theme, 1);
    await page.locator('[data-action="0"]').click();
    // Real Bank grant API, through the same native sim_action used by desktop tests.
    await native("game._sim_action(5)");
    await page.locator("#save").click();
    const before = await snapshot();
    assert.ok(before.tank.sd_balance >= 25);
    const saved = await page.evaluate(key => localStorage.getItem(key), KEY);
    const archive = JSON.parse(saved);
    assert.ok(archive.files.some(file => file.path.startsWith("tank.sav.profiles/")));
    assert.ok(archive.files.some(file => file.path === "device.bin"));
    // Refresh restoration occurs before init, including mode and Bank.
    await page.reload(); await ready();
    assert.equal((await snapshot()).tank.sd_balance, before.tank.sd_balance);
    assert.equal((await snapshot()).tank.clock_theme, 1);
    assert.equal(await native("return game._sim_scene_detail()"), 1);
    assert.equal(await native("return game._sim_mode_chosen()"), 1);
    // Background/pagehide uses synchronous persistence: make an unsaved change, then navigate away.
    await native("game._sim_action(5)");
    const afterHide = (await snapshot()).tank.sd_balance;
    await page.goto("about:blank");
    await page.goto(url); await ready();
    assert.equal((await snapshot()).tank.sd_balance, afterHide);
    await page.close();
    const reopened = await context.newPage();
    await reopened.goto(url); await ready(reopened);
    const reopenedBalance = await reopened.evaluate(async () => {
      const {game} = await import(document.querySelector('script[type="module"]').src);
      const p = game._sim_backup();
      try { return JSON.parse(game.UTF8ToString(p)).tank.sd_balance; } finally { game._sim_free(p); }
    });
    assert.equal(reopenedBalance, afterHide);
    // A second tab cannot overwrite the first.
    const second = await context.newPage();
    await second.goto(url);
    await second.waitForFunction(() => document.querySelector("#status").textContent.includes("Another tab"));
    await second.close();
    // Quota / disabled storage failures are visible, retryable and exportable.
    await reopened.evaluate(() => {
      const original = Storage.prototype.setItem;
      Storage.prototype.setItem = function(key, value) {
        if (key === "pocket-tank-browser-v1") throw new DOMException("Test quota failure", "QuotaExceededError");
        return original.call(this, key, value);
      };
      globalThis.restoreStorage = () => { Storage.prototype.setItem = original; };
    });
    await reopened.locator("#save").click();
    assert.ok((await reopened.locator("#status").textContent()).includes("Test quota failure"));
    const downloadPromise = reopened.waitForEvent("download");
    await reopened.locator("#export").click();
    const download = await downloadPromise;
    const stream = await download.createReadStream();
    const chunks = [];
    for await (const chunk of stream) chunks.push(chunk);
    const exported = Buffer.concat(chunks);
    assert.equal(JSON.parse(exported).format, "pocket-tank-browser");
    await reopened.evaluate(() => globalThis.restoreStorage());
    await reopened.locator("#save").click();
    await ready(reopened);
    // Full archive import uses validated paths, replaces only after confirmation.
    reopened.once("dialog", dialog => dialog.accept());
    await Promise.all([reopened.waitForNavigation(),
      reopened.locator("#file").setInputFiles({name: "browser.json", mimeType: "application/json", buffer: exported})]);
    await ready(reopened);
    // Shape-valid archives must pass actual native load before replacing saves.
    const corruptions = [
      ["profile", archive => {
        let copies = 0;
        for (const file of archive.files) {
          if (/^tank\.sav\.profiles\/.*-0\.[ab]$/.test(file.path)) {
            file.data = Buffer.from("broken native profile").toString("base64"); copies++;
          }
        }
        assert.equal(copies, 2, "Corrupt both profile recovery copies");
      }],
      ["preferences", archive => {
        const file = archive.files.find(file => file.path === "device.bin");
        assert.ok(file);
        file.data = Buffer.from([255, 1, 1, 0]).toString("base64");
      }],
      ["preferences trailing bytes", archive => {
        archive.files.find(file => file.path === "device.bin").data =
          Buffer.from([0, 1, 1, 0, 99]).toString("base64");
      }],
      ["brightness", archive => {
        archive.files.push({path: "tank.sav.brightness", data: Buffer.from([255]).toString("base64")});
      }],
    ];
    for (const [label, damage] of corruptions) {
      const archive = JSON.parse(exported);
      damage(archive);
      const text = JSON.stringify(archive);
      const knownGood = await reopened.evaluate(key => localStorage.getItem(key), KEY);
      let confirmations = 0;
      const dialog = async modal => { confirmations++; await modal.dismiss(); };
      reopened.on("dialog", dialog);
      await reopened.locator("#file").setInputFiles({name: "corrupt.json", mimeType: "application/json",
        buffer: Buffer.from(text)});
      await reopened.waitForFunction(() => document.querySelector("#status").classList.contains("error"));
      assert.match(await reopened.locator("#status").textContent(), /corrupt|unreadable/i, label);
      assert.equal(await reopened.evaluate(key => localStorage.getItem(key), KEY), knownGood,
        label + " import must preserve exact prior save");
      assert.equal(confirmations, 0, "Invalid native import rejected before replace confirmation");
      reopened.off("dialog", dialog);
      await reopened.locator("#save").click(); await ready(reopened);
      // Startup with the same native corruption must not present a fresh game.
      await reopened.evaluate(({key, text}) => localStorage.setItem(key, text), {key: KEY, text});
      await reopened.reload();
      await reopened.waitForFunction(() => document.querySelector("#save-detail").textContent.includes("Startup failed"));
      assert.match(await reopened.locator("#status").textContent(), /corrupt|unreadable/i, label);
      assert.equal(await reopened.evaluate(key => localStorage.getItem(key), KEY), text);
      assert.equal(await reopened.locator('[data-action="0"]').isDisabled(), true);
      await reopened.evaluate(({key, text}) => localStorage.setItem(key, text), {key: KEY, text: knownGood});
      await reopened.reload(); await ready(reopened);
    }
    // Corrupt startup never silently replaces a user's bytes.
    await reopened.evaluate(key => localStorage.setItem(key, "{bad archive"), KEY);
    await reopened.reload();
    await reopened.waitForFunction(() => document.querySelector("#save-detail").textContent.includes("Startup failed"));
    assert.equal(await reopened.evaluate(key => localStorage.getItem(key), KEY), "{bad archive");
    reopened.once("dialog", dialog => dialog.accept());
    await Promise.all([reopened.waitForNavigation(), reopened.locator("#reset").click()]);
    await ready(reopened);
    assert.equal(await reopened.locator('[data-action="0"]').isDisabled(), true);
    await context.close();
    // Mobile pointer input and responsive canvas; scroll prevention is canvas-only.
    const mobile = await browser.newContext({viewport: {width: 390, height: 844}, isMobile: true, hasTouch: true});
    const phone = await mobile.newPage();
    await phone.goto(url); await ready(phone);
    let box = await phone.locator("#screen").boundingBox();
    assert.ok(box.width <= 390 && box.height > 150);
    await phone.touchscreen.tap(box.x + 200 / 448 * box.width, box.y + 154 / 368 * box.height);
    await phone.touchscreen.tap(box.x + 200 / 448 * box.width, box.y + 300 / 368 * box.height);
    await phone.waitForFunction(() => !document.querySelector('[data-action="0"]').disabled);
    assert.equal(await phone.evaluate(async () => {
      const {game} = await import(document.querySelector('script[type="module"]').src);
      return game._sim_mode();
    }), 1, "Creative first-run must be playable through touchscreen");
    assert.equal(await phone.locator("#screen").evaluate(element => getComputedStyle(element).touchAction), "none");
    assert.notEqual(await phone.locator("body").evaluate(element => getComputedStyle(element).touchAction), "none");
    await mobile.close();
    // A real browser restart with a persistent on-disk test profile.
    const profile = path.resolve(__dirname, "..", ".browser-tools", "test-browser-profile");
    fs.rmSync(profile, {recursive: true, force: true});
    const launch = () => chromium.launchPersistentContext(profile, {headless: true,
      ...(process.argv.includes("--edge") ? {channel: "msedge"} : {})});
    let persistent;
    try {
      persistent = await launch();
      let tab = persistent.pages()[0];
      await tab.goto(url); await ready(tab);
      box = await tab.locator("#screen").boundingBox();
      await tab.mouse.click(box.x + 200 / 448 * box.width, box.y + 210 / 368 * box.height);
      await tab.mouse.click(box.x + 200 / 448 * box.width, box.y + 300 / 368 * box.height);
      await tab.locator("#save").click();
      await persistent.close(); persistent = null;
      persistent = await launch();
      tab = persistent.pages()[0];
      await tab.goto(url); await ready(tab);
      assert.equal(await tab.evaluate(async () => {
        const {game} = await import(document.querySelector('script[type="module"]').src);
        return game._sim_mode_chosen() && game._sim_mode();
      }), 2, "Hard profile must survive a complete browser restart");
    } finally {
      if (persistent) await persistent.close();
      fs.rmSync(profile, {recursive: true, force: true});
    }
    assert.deepEqual(errors, []);
    console.log("PASS: real WASM rendering and Bubble Pop, all first-run modes, UI/touch, durable Bank/profile saves, pagehide, refresh/reopen/browser restart, exclusive tabs, quota recovery/export/import/reset and corrupt startup.");
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => server?.close());
