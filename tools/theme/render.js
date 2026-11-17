// node render.js in.svg out.png W H  -> rasterise an SVG with Chromium (exact size)
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [inp, out, w, h] = process.argv.slice(2);
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: +w, height: +h } });
  await p.setContent(`<body style="margin:0">${fs.readFileSync(inp, 'utf8')}</body>`);
  await p.screenshot({ path: out });
  await b.close();
})();
