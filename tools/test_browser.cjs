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
    async function restoreFixture(fixture) {
      assert.equal(await native(`
        const bytes = new TextEncoder().encode(${JSON.stringify(JSON.stringify(fixture))});
        const pointer = game._malloc(bytes.length);
        try { game.HEAPU8.set(bytes, pointer); return game._sim_restore(pointer, bytes.length); }
        finally { game._free(pointer); }`), 1);
    }
    async function tap(x, y) {
      await page.locator("#screen").scrollIntoViewIfNeeded();
      const position = await canvasPoint(x / 448, y / 368);
      await page.mouse.click(position.x, position.y);
    }
    const priceHeader = fs.readFileSync(path.join(__dirname, "..", "common", "progression.h"), "utf8");
    const prices = new Map([...priceHeader.matchAll(/#define (SD_PRICE_\w+)\s+(\d+)/g)]
      .map(([, name, price]) => [name, Number(price)]));
    const catalogSource = fs.readFileSync(path.join(__dirname, "..", "common", "progression.c"), "utf8")
      .match(/const sd_item_t SD_ITEMS\[[^\]]+\] = \{([\s\S]*?)\n\};/)[1];
    const catalog = [...catalogSource.matchAll(/\{\s*[^,]+,\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*(\w+)\s*\}/g)]
      .map(([, price], item) => ({item, price: prices.has(price) ? prices.get(price) : Number(price)}))
      .sort((a, b) => a.price - b.price);
    assert.equal(catalog.length, 43);
    async function openShopItem(item) {
      await page.locator('[data-action="0"]').click();
      await page.locator('[data-action="3"]').click();
      const save = await snapshot();
      const row = catalog.findIndex(entry => entry.item === item) + ((save.tank.tank_ms_bits & (1 << 14)) ? 1 : 0);
      assert.ok(row >= 0);
      for (let p = 0; p < Math.floor(row / 4); p++) await tap(400, 50);
      await tap(120, 108 + row % 4 * 56);
    }
    await page.goto(url); await ready();
    assert.equal(await page.title(), "Pocket Tank CYD · Browser simulator");
    assert.equal(await page.locator("h1").textContent(), "Pocket Tank CYD");
    const build = await page.evaluate(async () => (await fetch("play-build.json", {cache:"no-store"})).json());
    assert.match(build.commit, /^[0-9a-f]{40}$/);
    const info = await page.locator("#build-info").textContent();
    assert.ok(info.includes(build.commit.slice(0, 7)));
    if (build.release) {
      assert.equal(build.release, "v" + build.version);
      assert.ok(info.includes("Released game " + build.release));
    } else assert.ok(info.includes("Development game " + build.version));
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
    async function canvasPoint(fx, fy) {
      return page.locator("#screen").evaluate((canvas, [fx, fy]) => {
        const box = canvas.getBoundingClientRect();
        const scale = Math.min(box.width / canvas.width, box.height / canvas.height);
        return {x: box.x + (box.width - canvas.width * scale) / 2 + fx * canvas.width * scale,
          y: box.y + (box.height - canvas.height * scale) / 2 + fy * canvas.height * scale};
      }, [fx, fy]);
    }
    async function feedAt(fraction) {
      const before = await native("return Array.from({length:256}, (_,i) => game._sim_food_x(i))");
      await page.locator("#feed").click();
      await page.locator("#screen").scrollIntoViewIfNeeded();
      const position = await canvasPoint(fraction, .65);
      await page.mouse.click(position.x, position.y);
      const after = await native("return Array.from({length:256}, (_,i) => game._sim_food_x(i))");
      const width = await native("return game._sim_width()");
      const dropped = after.filter((x, i) => before[i] < 0 && x >= 0);
      assert.equal(dropped.length, 3, `Place food must add exactly three pellets at ${JSON.stringify(position)}; ${await page.locator("#game-message").textContent()}; ${JSON.stringify(await page.locator("#screen").boundingBox())}`);
      for (const x of dropped) assert.ok(Math.abs(x - fraction * width) <= 15, `Food ${x} must drop near chosen column ${fraction * width}`);
      assert.equal(await page.locator("#feed").getAttribute("aria-pressed"), "false");
    }
    await page.locator("#touch-hints").click();
    assert.equal(await page.locator("#touch-hints").getAttribute("aria-pressed"), "true");
    await feedAt(.25); await feedAt(.75);
    await page.reload(); await ready();
    await page.locator('[data-action="0"]').click();
    await page.setViewportSize({width: 1100, height: 450});
    await feedAt(.4); // Normal-page canvas is letterboxed by its height cap.
    await page.locator("#fullscreen").click();
    await page.waitForFunction(() => document.fullscreenElement?.id === "game-panel" &&
      document.querySelector("#fullscreen").textContent === "Exit fullscreen");
    assert.equal(await page.locator("#fullscreen").textContent(), "Exit fullscreen");
    await feedAt(.6);
    await page.locator("#feed").click();
    const beforeMargin = await native("return Array.from({length:256}, (_,i) => game._sim_food_x(i)).filter(x=>x>=0).length");
    const margin = await page.locator("#screen").boundingBox();
    await page.mouse.click(margin.x + 2, margin.y + margin.height / 2);
    assert.equal(await native("return Array.from({length:256}, (_,i) => game._sim_food_x(i)).filter(x=>x>=0).length"), beforeMargin,
      "Fullscreen letterbox margins must not feed or select");
    assert.equal(await page.locator("#feed").getAttribute("aria-pressed"), "true");
    await page.locator("#feed").click();
    await page.locator("#fullscreen").click();
    await page.waitForFunction(() => !document.fullscreenElement &&
      document.querySelector("#fullscreen").textContent === "Fullscreen");
    assert.equal(await page.locator("#fullscreen").textContent(), "Fullscreen");
    await page.reload(); await ready();
    await page.setViewportSize({width: 1100, height: 1000});
    await page.locator('[data-action="2"]').click();
    await tap(100, 100); await tap(200, 138);
    await page.locator('[data-action="0"]').click();
    assert.equal(await native("return game._sim_width()"), 240);
    await feedAt(.3);
    await page.locator('[data-action="2"]').click();
    await tap(100, 100); await tap(200, 138);
    await page.locator('[data-action="0"]').click();
    for (const fraction of [.2, .5, .8]) {
      await page.reload(); await ready();
      await feedAt(fraction);
    }
    assert.equal(await page.locator("[data-feed], #feed-options").count(), 0, "No redundant feeding-position buttons");
    assert.equal(await native("return game._sim_feed_at(NaN) + game._sim_feed_at(-1) + game._sim_feed_at(game._sim_width())"), 0);
    await native("for(let i=0;i<8;i++) game._sim_feed_at(160)");
    await page.locator("#feed").click();
    await tap(224,200);
    assert.ok((await page.locator("#game-message").textContent()).includes("maximum pellets"));
    assert.equal(await page.locator("#feed").getAttribute("aria-pressed"), "true");
    await page.locator("#feed").click();
    await page.evaluate(() => { document.querySelector("#game-panel").requestFullscreen = undefined; });
    await page.locator("#fullscreen").click();
    assert.ok((await page.locator("#game-message").textContent()).includes("not supported"));
    await page.evaluate(() => { delete document.querySelector("#game-panel").requestFullscreen; });
    for (const [action, expected] of [[1, 1], [2, 2], [3, 3], [7, 9], [0, 0]]) {
      await page.locator(`[data-action="${action}"]`).click();
      assert.equal(await native("return game._sim_page()"), expected);
    }
    async function selectFish() {
      await page.locator('[data-action="0"]').click();
      for (let y = 65; y < 310; y += 40) {
        for (let x = 40; x < 430; x += 40) {
          await tap(x, y);
          if (await native("return game._sim_selected() >= 0 && game._sim_selected() < 20")) return;
          if (await native("return game._sim_page() !== 0 || game._sim_selected() >= 20"))
            await page.locator('[data-action="0"]').click();
        }
      }
      assert.fail("A real tank tap must open a fish card");
    }
    for (let scale = 0; scale < 2; scale++) {
      await selectFish();
      const selected = await native("return game._sim_selected()");
      const locked = (await snapshot()).tank.tools_locked;
      await tap(172, 315);
      assert.equal((await snapshot()).tank.tools_locked, locked ? 0 : 1, "Padlock must toggle");
      assert.equal(await native("return game._sim_selected()"), selected, "Lock must leave the card open");
      await tap(172, 315);
      assert.equal((await snapshot()).tank.tools_locked, locked, "Padlock must unlock again");
      await tap(45, 315);
      assert.equal(await native("return game._sim_tool()"), 1, "Sponge must be selectable");
      assert.equal(await native("return game._sim_selected()"), -1);
      await tap(230, 130);
      assert.equal(await native("return game._sim_tool()"), 1, "Tool owns the tank until DONE");
      await tap(60, 28);
      assert.equal(await native("return game._sim_tool()"), 0, "DONE must return to bare hands");
      await selectFish();
      await tap(105, 315);
      assert.equal(await native("return game._sim_tool()"), 2, "Scissors must be selectable");
      await tap(60, 28);
      assert.equal(await native("return game._sim_tool()"), 0);
      await page.locator('[data-action="2"]').click();
      await tap(100, 100); await tap(200, 138);
      await page.locator('[data-action="0"]').click();
    }
    await selectFish();
    const savedLock = (await snapshot()).tank.tools_locked;
    await tap(172, 315);
    await page.reload(); await ready();
    assert.equal((await snapshot()).tank.tools_locked, savedLock ? 0 : 1, "Lock must survive reload");
    await selectFish(); await tap(172, 315);
    await page.locator('[data-action="0"]').click();
    async function menuImage(name) {
      await page.waitForTimeout(60);
      const image = await page.locator("#screen").evaluate(canvas => canvas.toDataURL());
      if (process.env.POCKET_TANK_SCREENSHOTS) {
        fs.mkdirSync(process.env.POCKET_TANK_SCREENSHOTS, {recursive:true});
        fs.writeFileSync(path.join(process.env.POCKET_TANK_SCREENSHOTS, name + ".png"), Buffer.from(image.split(",")[1], "base64"));
      }
      return image;
    }
    async function checkArtHeader() {
      const colors = await page.locator("#screen").evaluate(canvas => {
        const context = canvas.getContext("2d");
        const data = context.getImageData(Math.ceil(326 * canvas.width / 448), Math.ceil(8 * canvas.height / 368),
          Math.floor(112 * canvas.width / 448), Math.floor(36 * canvas.height / 368)).data;
        const colors = new Set();
        for (let i = 0; i < data.length; i += 4) colors.add(`${data[i]},${data[i+1]},${data[i+2]}`);
        return colors.size;
      });
      assert.ok(colors >= 3, "BACK stays visible alongside the polished art");
    }
    for (let scale = 0; scale < 2; scale++) {
      const fixture = await snapshot();
      fixture.tank.sd_balance = 2000;
      fixture.care.ready = 7; fixture.care.claimed = 0; fixture.care.game_paid = 5;
      fixture.game_income.version = 1; fixture.game_income.legacy = 0;
      fixture.game_income.paid = Array(18).fill(0); fixture.game_income.paid[0] = 5;
      fixture.tank.setup_pending = 0; fixture.tank.newborn_p1 = 0;
      assert.equal(await native(`
        // Bootstrap a synthetic profile once; later restores must preserve live quotas.
        if (${scale} === 0) {
          for (const path of ["tank.sav", ...["0.a","0.b","1.a","1.b"].map(
            suffix => "tank.sav.profiles/5052463141435456-" + suffix)]) {
            if (game.FS.analyzePath(path).exists) game.FS.unlink(path);
          }
        }
        const bytes = new TextEncoder().encode(${JSON.stringify(JSON.stringify(fixture))});
        const pointer = game._malloc(bytes.length);
        try { game.HEAPU8.set(bytes, pointer); return game._sim_restore(pointer, bytes.length); }
        finally { game._free(pointer); }`), 1);
      await page.locator('[data-action="7"]').click();
      const home = await menuImage(`art-home-${scale}`);
      for (const [name, x, y] of [["fish",100,100], ["tank",330,100], ["collections",330,180], ["adventures",100,180]]) {
        await tap(x,y);
        assert.notEqual(await menuImage(`art-${name}-${scale}`), home);
        await checkArtHeader();
        if (name === "fish") {
          await tap(350,335);
          await menuImage(`art-fish-more-${scale}`);
          await checkArtHeader();
          await tap(350,335);
        }
        await tap(375,25);
        assert.equal(await menuImage(`art-home-back-${scale}`), home);
      }
      await tap(100,260);
      for (let gamePage = 0; gamePage < 3; gamePage++) {
        await menuImage(`art-games-${scale}-${gamePage}`);
        await tap(350,80);
      }
      await tap(330,330);
      await menuImage(`art-records-${scale}`);
      await checkArtHeader();
      await tap(375,25);
      await tap(375,25);
      await tap(330,180); await tap(100,100);
      const daily = await menuImage(`daily-care-${scale}`);
      const beforeCare = await snapshot();
      const balance = beforeCare.tank.sd_balance, xp = beforeCare.tank.journal_xp;
      const remainingReward = rewards => rewards.reduce((sum, reward, task) =>
        sum + (beforeCare.care.claimed & (1 << task) ? 0 : reward), 0);
      await tap(350,330);
      assert.notEqual(await menuImage(`milestones-${scale}`), daily);
      await tap(350,330);
      for (const y of [123,187,251]) { await tap(350,y); await tap(350,y); }
      const careSaved = await snapshot();
      assert.equal(careSaved.care.claimed, 7);
      assert.equal(careSaved.care.game_paid, 5);
      assert.equal(careSaved.tank.sd_balance, balance + remainingReward([30,30,65]));
      assert.equal(careSaved.tank.journal_xp, xp + remainingReward([12,13,25]));
      assert.equal(careSaved.mussel.owned, beforeCare.mussel.owned, "Daily care does not grant a shop creature");
      await menuImage(`daily-care-saved-${scale}`);
      await page.locator("#save").click(); await page.reload(); await ready();
      assert.equal((await snapshot()).care.claimed, 7, "Daily care allowance survives browser reload");
      assert.equal((await snapshot()).care.game_paid, 5, "Games share a persistent profile allowance");
      if (!(await snapshot()).mussel.owned) {
        const beforeBuy = await snapshot();
        await openShopItem(42);
        await menuImage(`mussel-shop-buy-${scale}`);
        await tap(220, 260);
        assert.equal((await snapshot()).mussel.owned, 1, "Mussel is bought in the ordinary shop");
        assert.equal((await snapshot()).tank.sd_balance, beforeBuy.tank.sd_balance - 120);
      }
      const pearlFixture = await snapshot();
      for (const fish of pearlFixture.fish) { fish.hunger = 2; fish.stress = 0; }
      await restoreFixture(pearlFixture);
      await page.locator('[data-action="0"]').click();
      const beforeIncome = await snapshot();
      await native("for (let i = 0; i < 2404; i++) game._sim_frame(.25)");
      const afterIncome = await snapshot();
      assert.equal(afterIncome.mussel.bank, 0, "Healthy live simulation pays automatically without a collection menu");
      assert.equal(afterIncome.mussel.generated, beforeIncome.mussel.generated + 1);
      assert.equal(afterIncome.tank.sd_balance, beforeIncome.tank.sd_balance + 1);
      assert.equal(afterIncome.tank.journal_xp, beforeIncome.tank.journal_xp);
      await menuImage(`mussel-income-glint-${scale}`);
      const cardFixture = await snapshot();
      cardFixture.tank.sd_unlocks |= 2;
      cardFixture.tank.snail_x = 30;
      cardFixture.tank.snail_y = 100;
      await restoreFixture(cardFixture);
      await page.locator('[data-action="0"]').click();
      const frameHeight = await native("return game._sim_height()");
      const clock = (await snapshot()).tank.clock;
      const anchors = [.82, .68, .48, .30, .46, .64];
      const stop = Math.floor((clock % 2160) / 360);
      const travel = Math.max(clock % 360 - 344, 0) / 16;
      const ease = travel * travel * (3 - 2 * travel);
      const musselX = anchors[stop] + (anchors[(stop + 1) % anchors.length] - anchors[stop]) * ease;
      await tap(448 * musselX, 368 * (frameHeight - 11) / frameHeight);
      assert.equal(await native("return game._sim_selected()"), 95, "Tapping the resident mussel opens its information card");
      await menuImage(`mussel-info-${scale}`);
      const beforeColors = await snapshot();
      await tap(224, 307);
      assert.equal(await native("return game._sim_page()"), 9, "Mussel COLORS opens the shared creature editor");
      const naturalMussel = await menuImage(`mussel-colors-natural-${scale}`);
      await tap(220, 235);
      await tap(220, 270);
      assert.notEqual(await menuImage(`mussel-colors-draft-${scale}`), naturalMussel);
      assert.equal((await snapshot()).companion.mussel_colors || 0, beforeColors.companion.mussel_colors || 0, "Preview is not saved implicitly");
      await tap(110, 309);
      const savedColors = (await snapshot()).companion.mussel_colors;
      assert.ok(savedColors > 0, "Primary/detail mussel colors save");
      assert.equal((await snapshot()).tank.sd_balance, beforeColors.tank.sd_balance, "Mussel colors are free");
      await page.locator("#save").click(); await page.reload(); await ready();
      assert.equal((await snapshot()).companion.mussel_colors, savedColors, "Mussel colors survive reload");
      await page.locator('[data-action="0"]').click();
      await tap(448 * musselX, 368 * (frameHeight - 11) / frameHeight);
      assert.equal(await native("return game._sim_selected()"), 95);
      await menuImage(`mussel-info-colored-${scale}`);
      const cardState = await snapshot();
      await tap(362, 69);
      assert.equal(await native("return game._sim_selected()"), 99, "Visible creature arrow selects the owned snail");
      assert.equal((await snapshot()).mussel.bank, cardState.mussel.bank, "Creature cycling never collects income");
      assert.equal((await snapshot()).mussel.generated, cardState.mussel.generated);
      await menuImage(`creature-next-${scale}`);
      await openShopItem(9);
      if (!((await snapshot()).tank.sd_unlocks & (1 << 9))) {
        await menuImage(`chest-first-buy-${scale}`);
        await tap(220, 260);
        await tap(375, 28);
        await openShopItem(9);
      }
      await menuImage(`shop-copy-${scale}`);
      const inventoryBefore = await snapshot();
      const storedBefore = Buffer.from(inventoryBefore.inventory.stored, "hex")[5];
      const chestCount = save => ((save.tank.sd_unlocks >>> 9) & 1) +
        [...Buffer.from(save.inventory.extra, "hex")].filter((n, i) => i % 8 === 0 && n === 10).length;
      const placedBefore = chestCount(inventoryBefore);
      await tap(220,276);
      const armedSale = await menuImage(`inventory-sale-armed-${scale}`);
      await tap(344,64);
      assert.notEqual(await menuImage(`inventory-back-${scale}`), armedSale, "Explicit Back closes the item panel");
      await openShopItem(9);
      assert.notEqual(await menuImage(`inventory-reopened-${scale}`), armedSale, "Back disarms an unconfirmed sale");
      assert.deepEqual((await snapshot()).inventory, inventoryBefore.inventory, "Back never sells or moves a copy");
      assert.equal((await snapshot()).tank.sd_balance, inventoryBefore.tank.sd_balance);
      await tap(280,150); await tap(280,150);
      assert.equal(Buffer.from((await snapshot()).inventory.stored, "hex")[5], storedBefore + 2);
      assert.equal((await snapshot()).tank.sd_balance, inventoryBefore.tank.sd_balance - 240);
      for (let copy = 0; copy < 2; copy++) {
        await tap(120,150);
        assert.equal(await native("return game._sim_page()"), 0);
        await menuImage(`inventory-place-${scale}-${copy}`);
        await tap(375,28);
        await openShopItem(9);
      }
      assert.equal(chestCount(await snapshot()), placedBefore + 2);
      await tap(280,230);
      assert.equal(chestCount(await snapshot()), placedBefore + 1);
      const inventorySaved = (await snapshot()).inventory;
      await page.locator("#save").click(); await page.reload(); await ready();
      assert.deepEqual((await snapshot()).inventory, inventorySaved, "Duplicate placements and shared storage survive reload");
      const viewerSave = await snapshot();
      const viewer = await context.newPage();
      await viewer.setContent(fs.readFileSync(path.join(__dirname, "..", "web", "viewer.html"), "utf8"));
      await viewer.locator("#file").setInputFiles({name: "synthetic-inventory.json", mimeType: "application/json",
        buffer: Buffer.from(JSON.stringify(viewerSave))});
      await viewer.waitForSelector("#out:not([hidden])");
      assert.equal(await viewer.locator("#error").textContent(), "");
      const viewerFact = name => viewer.locator("#overview tr").filter({hasText: name}).locator("td").textContent();
      assert.equal(await viewerFact("Shared decoration storage"),
        `${[...Buffer.from(viewerSave.inventory.stored, "hex")].reduce((sum, n) => sum + n, 0)}/64 pieces`);
      assert.equal(await viewerFact("Extra ornaments here"),
        `${[...Buffer.from(viewerSave.inventory.extra, "hex")].filter((n, i) => i % 8 === 0 && n !== 0).length}/8 placed`);
      assert.equal(await viewerFact("Pearl mussel"),
        `${viewerSave.mussel.owned.toString(2).replace(/0/g, "").length} aquarium resident(s); ${viewerSave.mussel.generated}/15 earned today; ${viewerSave.mussel.bank} pending automatic payout`);
      assert.equal(await viewerFact("Game bonus today"),
        `${viewerSave.care.game_paid}/90 sand dollars; up to 5 per game; no XP`);
      await viewer.close();
      await page.locator('[data-action="2"]').click();
      await menuImage(`settings-${scale}`);
      await tap(330, 260); // Device.
      await tap(224, 69); // Screenshot to SD is discoverable outside backups.
      assert.equal(await native("return game._sim_page()"), 2);
      const beforeWifi = (await snapshot()).tank;
      await tap(200, 122); // Firmware installation deliberately stays hardware-only.
      assert.equal(await native("return game._sim_page()"), 2);
      assert.equal(await native("return game.UTF8ToString(game._sim_error())"), "FIRMWARE UPDATES ARE ON THE CYD");
      const afterWifi = (await snapshot()).tank;
      for (const key of ["sd_balance", "journal_xp", "sd_unlocks", "newborn_p1"])
        assert.equal(afterWifi[key], beforeWifi[key], `Firmware notice must preserve ${key}`);
      await tap(380, 28); // Device -> settings hub.
      await tap(330, 100);
      const hub = await menuImage(`clock-hub-${scale}`);
      const beforeMenus = (await snapshot()).tank;
      for (const [x,y,name] of [[100,100,"face"], [330,100,"colors"], [100,180,"text"], [330,180,"time"]]) {
        await tap(x,y);
        const child = await menuImage(`clock-${name}-${scale}`);
        assert.notEqual(child, hub, `${name} must have its own screen`);
        await tap(220, 108); // Row gap is not a control.
        assert.equal(await menuImage(`clock-${name}-gap-${scale}`), child);
        await tap(380,28);
        assert.equal(await menuImage(`clock-back-${scale}`), hub, "One BACK returns to the hub");
      }
      await tap(100,260);
      const captionBefore = (await snapshot()).tank.clock_delay;
      for (let textSize = 0; textSize < 3; textSize++) {
        await tap(320,288);
        const delay = (await snapshot()).tank.clock_delay;
        assert.equal(delay & ~24, captionBefore & ~24);
        assert.equal((delay & 24) >> 3, ((((captionBefore & 24) >> 3) + textSize + 1) % 3));
        await menuImage(`weather-size-${scale}-${textSize}`);
      }
      await tap(390,28); await tap(100,260); // Reopen with a clear status message.
      const weather = await menuImage(`weather-${scale}`);
      await tap(140,234);
      const keyboard = await menuImage(`city-search-${scale}`);
      assert.notEqual(keyboard, weather);
      await tap(220,340); // Empty search must remain on the keyboard.
      await tap(390,28);
      assert.equal(await menuImage(`weather-back-${scale}`), weather);
      await tap(390,28);
      assert.equal(await menuImage(`weather-hub-${scale}`), hub);
      const afterMenus = (await snapshot()).tank;
      for (const key of ["clock_theme","clock_flags","clock_layout","clock_delay","clock_secondary","clock_utc_quarters"])
        assert.equal(afterMenus[key], beforeMenus[key], `Browsing must preserve ${key}`);
      await tap(380,28); await tap(100,260); // Settings -> Care.
      const autofeed = (await snapshot()).tank.autofeed_off;
      await tap(224,198);
      assert.equal((await snapshot()).tank.autofeed_off, autofeed ? 0 : 1);
      await tap(224,198);
      assert.equal((await snapshot()).tank.autofeed_off, autofeed);
      await menuImage(`care-${scale}`);
      await tap(380,28); await tap(330,180);
      await menuImage(`backups-${scale}`);
      const backupBefore = await snapshot();
      async function assertMenuPreserved() {
        const after = await snapshot();
        for (const key of ["names","n_fish","sd_balance","sd_earned","sd_unlocks","journal_xp",
          "newborn_p1","autofeed_off","clock_theme","clock_flags","clock_layout","clock_delay"]) {
          assert.ok(Object.hasOwn(backupBefore.tank, key), `Fixture contains ${key}`);
          assert.deepEqual(after.tank[key], backupBefore.tank[key], `Inert/cancelled control preserves ${key}`);
        }
        for (const key of ["bank","inventory","profile"]) {
          assert.ok(Object.hasOwn(backupBefore, key), `Fixture contains ${key}`);
          assert.deepEqual(after[key], backupBefore[key], `Inert/cancelled control preserves ${key}`);
        }
      }
      await tap(224,228); // Removed duplicate screenshot control is inert.
      await assertMenuPreserved();
      await tap(380,28); await tap(330,260);
      const deviceImage = await menuImage(`device-${scale}`);
      await tap(224,250); // Gap above destructive action is inert.
      assert.equal(await native("return game._sim_page()"), 2);
      assert.equal(await menuImage(`device-gap-${scale}`), deviceImage);
      await tap(224,284); // Wipe must open confirmation, never reset directly.
      assert.equal(await native("return game._sim_page()"), 2);
      assert.notEqual(await menuImage(`wipe-confirm-${scale}`), deviceImage);
      await tap(140,216); // Cancel the rendered confirmation without touching saves.
      assert.equal(await menuImage(`wipe-cancelled-${scale}`), deviceImage);
      await assertMenuPreserved();
      await tap(380,28); await tap(100,100); await tap(200,138);
      await page.locator('[data-action="0"]').click();
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
    await native("for (let i = 0; i < 120; i++) game._sim_frame(.25)");
    await menuImage("game-rewards");
    const beforeResult = await snapshot();
    await tap(224,218);
    const afterResult = await snapshot();
    assert.equal(afterResult.tank.sd_balance, beforeResult.tank.sd_balance, "No payout beyond this game's five-dollar allowance");
    assert.equal(afterResult.game_income.paid[0], 5);
    assert.equal(afterResult.game_income.paid[1], 0, "Other games retain independent daily allowances");
    assert.equal(afterResult.tank.journal_xp, beforeResult.tank.journal_xp, "Games never award journal XP");
    assert.equal(afterResult.companion.game_rounds[0], beforeResult.companion.game_rounds[0] + 1, "Records still save after daily bonus is exhausted");
    await menuImage("game-rewards-saved");
    await page.locator('[data-action="0"]').click();
    await page.locator('[data-action="2"]').click();
    await tap(100, 100); await tap(200, 192);
    assert.equal(await native("return game._sim_scene_detail()"), 1);
    await page.locator('[data-action="2"]').click();
    await tap(330, 100); await tap(100, 100); await tap(220, 84);
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
    await phone.locator("#feed").click();
    await phone.locator("#screen").scrollIntoViewIfNeeded();
    box = await phone.locator("#screen").boundingBox();
    await phone.touchscreen.tap(box.x + box.width * .75, box.y + box.height * .6);
    assert.ok((await phone.locator("#game-message").textContent()).includes("Food dropped"));
    await phone.locator("#fullscreen").click();
    await phone.waitForFunction(() => document.fullscreenElement?.id === "game-panel");
    await phone.locator("#feed").click();
    const visibleButtons = await phone.locator("#feed, #fullscreen").evaluateAll(buttons =>
      buttons.every(button => {
        const r = button.getBoundingClientRect();
        return r.width >= 44 && r.height >= 44 && r.left >= 0 && r.right <= innerWidth && r.bottom <= innerHeight;
      }));
    assert.ok(visibleButtons, "Fullscreen portrait controls must be usable 44px targets");
    box = await phone.locator("#screen").boundingBox();
    const imageWidth = await phone.locator("#screen").evaluate(canvas => Math.min(canvas.clientWidth, canvas.clientHeight * canvas.width / canvas.height));
    const imageHeight = imageWidth * 240 / 320;
    await phone.touchscreen.tap(box.x + box.width / 2 - imageWidth * .3, box.y + (box.height - imageHeight) / 2 + imageHeight * .6);
    assert.ok((await phone.locator("#game-message").textContent()).includes("Food dropped"));
    await phone.locator("#fullscreen").click();
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
    console.log("PASS: normal shop mussel/first-decoration purchases, integrated copies/place/store/reload, automatic healthy live income, care125 with unchanged XP and per-game5/day caps, schema18 viewer facts at both scales; Settings/Clock/Weather, fish-card tools, fullscreen/mobile, all modes, durable saves, exclusive tabs and corruption/quota recovery.");
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => server?.close());
