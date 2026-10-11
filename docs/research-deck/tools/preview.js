// node preview.js <slide ids...>  -> preview/<id>.png  (approximate local render)
const { chromium } = require('/opt/node-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
const D = __dirname;
const map = {};
for (const l of fs.readFileSync(D + '/blobs.txt', 'utf8').trim().split('\n')) { const [f, id] = l.split(' '); map[id] = 'file://' + path.join(D, f); }
(async () => {
  const ids = process.argv.slice(2);
  fs.mkdirSync(D + '/preview', { recursive: true });
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 } });
  for (const id of ids) {
    let h = fs.readFileSync(`${D}/root/project/slides/${id}.html`, 'utf8');
    h = h.replace(/\/_blob\/([0-9A-Za-z]+)/g, (m, k) => map[k] || m);
    h = h.replace(/<aside>[\s\S]*?<\/aside>/, '');
    // x-connector -> svg line
    h = h.replace(/<x-connector([^>]*)><\/x-connector>/g, (m, a) => {
      const g = n => { const r = a.match(new RegExp(n + '="([^"]+)"')); return r ? r[1] : null; };
      const x1 = +g('x1'), y1 = +g('y1'), x2 = +g('x2'), y2 = +g('y2'); const st = g('style') || '';
      const col = (st.match(/color:\s*([^;]+)/) || [0, '#333'])[1]; const w = (st.match(/border-width:\s*(\d+)/) || [0, 2])[1];
      const dash = /dashed/.test(st) ? 'stroke-dasharray="10 8"' : '';
      const head = g('head') || 'end';
      const mk = head === 'none' ? '' : 'marker-end="url(#ah)"';
      return `<svg style="position:absolute;left:0;top:0;overflow:visible" width="1" height="1"><defs><marker id="ah" orient="auto" markerWidth="4" markerHeight="4" refX="2" refY="2"><path d="M0,0 L4,2 L0,4 Z" fill="${col}"/></marker></defs><line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${col}" stroke-width="${w}" ${dash} ${mk}/></svg>`;
    });
    h = h.replace(/<x-shape kind="ellipse" style="([^"]*)"><\/x-shape>/g, (m, s) => `<div style="${s};border-radius:50%"></div>`);
    h = h.replace(/<x-shape kind="diamond" style="([^"]*)"><\/x-shape>/g, (m, s) => `<div style="${s};transform:rotate(45deg) scale(0.707)"></div>`);
    const html = `<html><head><link href="https://fonts.googleapis.com/css2?family=Inter:wght@300..800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet"><style>*{margin:0;padding:0;box-sizing:border-box} section{position:relative;width:1920px;height:1080px;overflow:hidden} ul,ol{padding-left:1.2em} td,th{padding:0.35em 0.6em;border-bottom:1px solid #ddd}</style></head><body>${h}</body></html>`;
    fs.writeFileSync(`${D}/preview/_t.html`, html); await p.goto('file://' + D + '/preview/_t.html', { waitUntil: 'networkidle' }).catch(() => {});
    await p.screenshot({ path: `${D}/preview/${id}.png` });
  }
  await b.close();
})();
