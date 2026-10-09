// Development-only: use an existing browser and externally installed Playwright.
const path = require('path');
const [playwrightModule, browserPath, outputDir, baseUrl = 'http://127.0.0.1:8765/', captureMode = 'all'] = process.argv.slice(2);
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
    const controllerGeometry = {};
    for (const selector of ['.controller', '.controller .head', '.ctl-grid', '.ai-box', '.aib-task',
                            '.aib-size', '.aib-go', '.aib-idle', '.aib-name', '.aib-top .status-dot',
                            '.aib-kind', '.aib-task-label', '.aib-prog', '.aib-bar', '.aib-note',
                            '.aib-live', '.aib-stats', '.ctl-footer', '.ctl-foot-mid', '.ctl-foot-mid .tb-btn']) {
      controllerGeometry[selector] = await page.locator(selector).evaluateAll(elements => elements.map(element => {
        const bounds = element.getBoundingClientRect();
        const style = getComputedStyle(element);
        return { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height,
                 font: style.font, background: style.background, border: style.border,
                 radius: style.borderRadius };
      }));
    }
    require('fs').writeFileSync(path.resolve(outputDir, 'controller-geometry.json'), JSON.stringify(controllerGeometry, null, 2));
    await page.screenshot({ path: path.resolve(outputDir, 'controller.png') });
    if (captureMode === 'tour') {
      if (!await page.locator('.tour-pop').count()) {
        await page.getByTitle('Settings - theme, repo, matching', { exact: true }).click();
        await page.getByRole('button', { name: "Replay Tango's tour", exact: true }).click();
      }
      await page.locator('.tour-pop').waitFor();
      const geometry = {};
      for (let step = 0; step < 2; step++) {
        geometry[step] = {};
        for (const selector of ['.tour-pop', '.tour-tango', '.tour-box', '.tour-box-title', '.tour-box-body', '.tour-nav', '.tour-spot']) {
          if (!await page.locator(selector).count()) continue;
          geometry[step][selector] = await page.locator(selector).evaluate(element => {
            const bounds = element.getBoundingClientRect();
            return { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height, font: getComputedStyle(element).font };
          });
        }
        await page.screenshot({ path: path.resolve(outputDir, `tour-${step}.png`) });
        if (!step) await page.locator('.tour-nav').getByRole('button', { name: 'Next', exact: true }).click();
      }
      require('fs').writeFileSync(path.resolve(outputDir, 'tour-geometry.json'), JSON.stringify(geometry, null, 2));
      console.log(JSON.stringify({ errors, calls: await page.evaluate(() => window.__referenceCalls) }));
      if (errors.length) throw new Error('Original tour reference reported errors');
      return;
    }
    if (captureMode === 'helper') {
      await page.getByTitle('Show tips', { exact: true }).click();
      await page.locator('.tango-tips').waitFor();
      const helperGeometry = {};
      for (const selector of ['.tango-helper', '.tango-tips', '.tt-head', '.tt-body', '.tt-body b', '.tt-body p', '.tt-nav', '.tango-mascot']) {
        if (!await page.locator(selector).count()) continue;
        helperGeometry[selector] = await page.locator(selector).evaluate(element => {
          const bounds = element.getBoundingClientRect();
          const style = getComputedStyle(element);
          return { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height, font: style.font };
        });
      }
      require('fs').writeFileSync(path.resolve(outputDir, 'helper-geometry.json'), JSON.stringify(helperGeometry, null, 2));
      await page.screenshot({ path: path.resolve(outputDir, 'helper.png') });
      console.log(JSON.stringify({ errors, calls: await page.evaluate(() => window.__referenceCalls) }));
      if (errors.length) throw new Error('Original helper reference reported errors');
      return;
    }
    if (captureMode === 'controller') {
      console.log(JSON.stringify({ errors, calls: await page.evaluate(() => window.__referenceCalls) }));
      if (errors.length) throw new Error('Original Controller reference reported errors');
      return;
    }
    await page.getByTitle('Report a bug', { exact: true }).click();
    await page.locator('.bug-report').waitFor();
    const reportGeometry = {};
    for (const selector of ['.bug-report', '.bug-report .head', '.bug-report .hint', '.bug-desc', '.bug-shots', '.bug-actions']) {
      reportGeometry[selector] = await page.locator(selector).evaluate(element => {
        const bounds = element.getBoundingClientRect();
        return { x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height };
      });
    }
    require('fs').writeFileSync(path.resolve(outputDir, 'report-geometry.json'), JSON.stringify(reportGeometry, null, 2));
    await page.screenshot({ path: path.resolve(outputDir, 'bug-report.png') });
    await page.locator('.bug-report .dock-close').click();
    await page.getByTitle('Open detailed stats, history, and recommendation').first().click();
    await page.locator('.ai-detail').waitFor();
    await page.screenshot({ path: path.resolve(outputDir, 'agent-detail.png') });
    await page.locator('.ai-detail .dock-close').click();
    await page.getByRole('button', { name: 'Chaos Viewer', exact: true }).click();
    await page.getByRole('heading', { name: /^Atlas/ }).waitFor();
    await page.waitForTimeout(3500); // Original tab-switch splash must finish before comparison.
    await page.screenshot({ path: path.resolve(outputDir, 'atlas.png') });
    console.log(JSON.stringify({ errors, calls: await page.evaluate(() => window.__referenceCalls) }));
    if (errors.length) throw new Error('Original reference UI reported errors');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
