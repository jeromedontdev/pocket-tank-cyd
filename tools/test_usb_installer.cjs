/* Run against checksum-staged _site. USB and the factory component are mocked;
 * real manifests, application downloads, board checks and UI handlers run. */
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
      const types = { ".html": "text/html", ".mjs": "text/javascript", ".json": "application/json",
        ".png": "image/png", ".svg": "image/svg+xml" };
      res.writeHead(error ? 404 : 200, { "Content-Type": types[path.extname(name)] || "application/octet-stream" });
      res.end(error ? "Not found" : data);
    });
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true,
      ...(process.argv.includes("--edge") ? { channel: "msedge" } : {}) });
    const base = `http://127.0.0.1:${server.address().port}`;
    for (const width of [1100, 375]) {
      const context = await browser.newContext({ viewport: { width, height: 900 } });
      const page = await context.newPage();
      const errors = [];
      page.on("pageerror", error => errors.push(error.message));
      await page.addInitScript(() => {
        window.testPortRequests = 0;
        window.testPortOpens = 0;
        Object.defineProperty(navigator, "serial", { value: {
          requestPort: async () => {
            window.testPortRequests++;
            return { open: async () => {
              window.testPortOpens++;
              throw new Error("Synthetic serial stop; no device used");
            } };
          },
        } });
      });
      await page.route("https://unpkg.com/**", route => route.fulfill({
        contentType: "text/javascript",
        body: 'customElements.define("esp-web-install-button", class extends HTMLElement {});',
      }));
      await page.goto(base + "/flash.html");
      assert.equal(await page.locator("#board").inputValue(), "");
      assert.equal(await page.locator("#acknowledge").isDisabled(), true);
      assert.equal(await page.locator("#update-acknowledge").isDisabled(), true);
      assert.equal(await page.locator("#safe-update").isDisabled(), true);
      assert.equal(await page.evaluate(() => window.testPortRequests), 0);
      await page.locator("#board").selectOption("cyd2432s028");
      await page.waitForFunction(() => document.querySelector("#status").textContent.startsWith("Ready to install"));
      assert.equal(await page.locator("esp-web-install-button").getAttribute("manifest"), "./manifest.json");
      let finishFactoryLoad;
      const factoryWait = new Promise(resolve => { finishFactoryLoad = resolve; });
      let factoryRequested;
      const factoryStarted = new Promise(resolve => { factoryRequested = resolve; });
      await page.route("**/elegoo-manifest.json", async route => {
        factoryRequested();
        await factoryWait;
        await route.continue();
      });
      await page.locator("#board").selectOption("elegoo28");
      await factoryStarted;
      assert.equal(await page.locator("#board").isDisabled(), true);
      assert.equal(await page.locator("#update-acknowledge").isDisabled(), true);
      assert.equal(await page.locator("#safe-update").isDisabled(), true);
      finishFactoryLoad();
      await page.waitForFunction(() => document.querySelector("#status").textContent.includes("for ELEGOO"));
      assert.equal(await page.locator("esp-web-install-button").getAttribute("manifest"), "./elegoo-manifest.json");
      await page.locator("#acknowledge").check();
      assert.equal(await page.locator("esp-web-install-button").isVisible(), true);
      assert.equal(await page.locator("#board").isDisabled(), true);
      assert.equal(await page.locator("#update-acknowledge").isDisabled(), true);
      await page.locator("#acknowledge").uncheck();

      const cyd = JSON.parse(fs.readFileSync(path.join(root, "update.json")));
      await page.route("**/elegoo-update.json", route => route.fulfill({ json: cyd }));
      await page.locator("#update-acknowledge").check();
      await page.locator("#safe-update").click();
      await page.waitForFunction(() => document.querySelector("#update-status").textContent.includes("Update stopped"));
      assert.match(await page.locator("#update-status").textContent(), /Invalid save-preserving update manifest/);
      assert.equal(await page.evaluate(() => window.testPortRequests), 1);
      assert.equal(await page.evaluate(() => window.testPortOpens), 0);
      await page.unroute("**/elegoo-update.json");

      let releaseDownload;
      const hold = new Promise(resolve => { releaseDownload = resolve; });
      let requested;
      const requestStarted = new Promise(resolve => { requested = resolve; });
      await page.route("**/elegoo-update.json", async route => {
        requested();
        await hold;
        await route.continue();
      });
      await page.locator("#safe-update").click();
      await requestStarted;
      assert.equal(await page.locator("#board").isDisabled(), true);
      assert.equal(await page.evaluate(() => document.querySelector("#factory-section").inert), true);
      releaseDownload();
      await page.waitForFunction(() => document.querySelector("#update-status").textContent.includes("Synthetic serial stop"));
      assert.equal(await page.evaluate(() => window.testPortRequests), 2);
      assert.equal(await page.evaluate(() => window.testPortOpens), 1);
      assert.equal(await page.locator("#board").isDisabled(), false);
      assert.equal(await page.evaluate(() => document.querySelector("#factory-section").inert), false);
      await page.locator("#board").selectOption("cyd2432s028");
      await page.waitForFunction(() => document.querySelector("#status").textContent.endsWith("for CYD."));
      assert.equal(await page.locator("#update-acknowledge").isChecked(), false);
      assert.equal(await page.locator("#acknowledge").isChecked(), false);
      await page.locator("#update-acknowledge").check();
      await page.locator("#safe-update").click();
      await page.waitForFunction(() => window.testPortOpens === 2 && !document.querySelector("#board").disabled);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      assert.deepEqual(errors, []);
      await page.goto(base + "/index.html");
      for (const name of ["tank-normal.png", "fish-summary.png", "family-tree.png"]) {
        const image = page.locator(`img[src="./media/${name}"]`);
        await image.scrollIntoViewIfNeeded();
        await page.waitForFunction(name => {
          const img = document.querySelector(`img[src="./media/${name}"]`);
          return img && img.complete && img.naturalWidth > 0;
        }, name);
        await image.evaluate(img => img.decode());
        assert.equal(await image.evaluate(img => img.naturalWidth > 0 && img.clientWidth <= innerWidth), true);
      }
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      assert.deepEqual(errors, []);
      await context.close();
    }
    console.log("PASS: desktop/mobile board selection, separate factory manifests, no update during board loading, cross-board refusal before opening serial, verified real apps, busy selection lock, acknowledgement reset and responsive release artwork");
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
