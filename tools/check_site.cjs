// Optional browser regression check. Requires Playwright and its Chromium.
const assert = require('node:assert/strict');
const {chromium} = require('playwright');

async function main() {
  const browser = await chromium.launch({
    headless: true,
    ...(process.env.CHROMIUM_PATH ? {executablePath: process.env.CHROMIUM_PATH} : {}),
    args: ['--enable-unsafe-swiftshader']
  });
  try {
    const page = await browser.newPage({viewport: {width: 1360, height: 1100}});
    const errors = [];
    const requests = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => requests.push(request.url()));
    const url = process.argv[2] || 'http://localhost:8000/';
    await page.goto(url);
    assert(!requests.some(path => path.endsWith('x16emu.js')), 'Emulator must load only after Play');
    await page.click('#play');
    await page.waitForFunction(() => document.querySelector('#status').textContent.startsWith('Game running'));
    await page.waitForTimeout(4000); // Allow the emulated CPU to reach the title.
    const files = await page.evaluate(() => FS.readdir('/'));
    for (const name of ['rom.bin', 'OTHELLO.PRG', 'TILES.DAT', 'FONTMAP.DAT', 'HELP.DAT', 'OTHELLO.ZSM', 'TILE.ZSM']) {
      assert(files.includes(name), `Emulated filesystem needs ${name}`);
    }

    // A legal first move at row 2, column 4 must render a stone at that cell.
    // Check the pixels, not merely whether JavaScript reports it has started.
    async function moveWithMouse() {
      await page.locator('#canvas').press('Enter');
      await page.waitForTimeout(500);
      const box = await page.locator('#canvas').boundingBox();
      const x = box.x + box.width * 336 / 640;
      const y = box.y + box.height * 208 / 480;
      await page.mouse.move(x, y);
      await page.waitForTimeout(2600); // First entry aligns the ROM and host pointers.
      await page.mouse.click(x, y, {delay: 150});
      await page.waitForTimeout(1000); // Also allow the computer's answering move.
      await page.mouse.move(box.x + 5, box.y + 5);
      await page.waitForTimeout(100);
      const shot = await page.screenshot({clip: {
        x: x - box.width * 12 / 640, y: y - box.height * 12 / 480,
        width: box.width * 24 / 640, height: box.height * 24 / 480
      }});
      const coloredPixels = await page.evaluate(async data => {
        const image = await createImageBitmap(await (await fetch(`data:image/png;base64,${data}`)).blob());
        const buffer = document.createElement('canvas');
        buffer.width = image.width; buffer.height = image.height;
        const context = buffer.getContext('2d');
        context.drawImage(image, 0, 0);
        const pixels = context.getImageData(0, 0, image.width, image.height).data;
        let count = 0;
        for (let i = 0; i < pixels.length; i += 4) {
          const [r, g, b] = pixels.slice(i, i + 3);
          if ((r > g + 50 && r > b + 50) || (g > r + 50 && b > r + 40)) count++;
        }
        return count;
      }, shot.toString('base64'));
      assert(coloredPixels > 10, 'Mouse move must place a visible red or teal stone');
    }
    await moveWithMouse();
    await page.click('#audio');
    assert.equal(await page.locator('#audio').getAttribute('aria-pressed'), 'true');
    await page.click('#audio');
    assert.equal(await page.locator('#audio').getAttribute('aria-pressed'), 'false');
    await page.click('#fullscreen');
    assert(await page.evaluate(() => !!document.fullscreenElement), 'Fullscreen must open');
    await page.evaluate(() => document.exitFullscreen());
    await page.locator('#canvas').press('Escape');
    await page.setViewportSize({width: 390, height: 844});
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'Mobile must not overflow horizontally');
    await moveWithMouse();
    assert.deepEqual(errors, [], 'No JavaScript exceptions');

    // Missing assets must show an error and leave a working restart action.
    const broken = await browser.newPage();
    await broken.route('**/game/TILES.DAT', route => route.fulfill({status: 404, body: 'Missing'}));
    await broken.goto(url);
    await broken.click('#play');
    await broken.locator('#error').waitFor({state: 'visible'});
    assert.equal(await broken.locator('#restart').isEnabled(), true);
    assert.equal(await broken.locator('#canvas').isVisible(), false);
    console.log('PASS: lazy loading, assets, actual game moves at desktop/mobile sizes, audio, fullscreen, and download failure');
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
