// Development-only: use an existing browser and externally installed Playwright.
const path = require('path');
const [playwrightModule, browserPath, outputDir, baseUrl = 'http://127.0.0.1:8765/'] = process.argv.slice(2);
if (!playwrightModule || !browserPath || !outputDir) {
  throw new Error('Usage: node reference-capture.cjs <playwright-module> <browser-executable> <output-dir> [loopback-url]');
}
const url = new URL(baseUrl);
if (!['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname)) throw new Error('Reference must be served on loopback');
const { chromium } = require(path.resolve(playwrightModule));
(async () => {
  const browser = await chromium.launch({ executablePath: browserPath, headless: true });
  try {
    const context = await browser.newContext({ viewport: { width: 1180, height: 820 }, deviceScaleFactor: 1 });
    await context.route('**/*', route => new URL(route.request().url()).origin === url.origin ? route.continue() : route.abort());
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(baseUrl, { waitUntil: 'networkidle' });
    await page.getByRole('heading', { name: 'Chaos Controller', exact: true }).waitFor();
    await page.screenshot({ path: path.resolve(outputDir, 'controller.png') });
    await page.getByRole('button', { name: 'Chaos Viewer', exact: true }).click();
    await page.getByRole('heading', { name: /^Atlas/ }).waitFor();
    await page.waitForTimeout(3500); // Original tab-switch splash must finish before comparison.
    await page.screenshot({ path: path.resolve(outputDir, 'atlas.png') });
    console.log(JSON.stringify({ errors, calls: await page.evaluate(() => window.__referenceCalls) }));
    if (errors.length) throw new Error('Original reference UI reported errors');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
