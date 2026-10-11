// usage: node tex2svg.js formulas.json outdir
const fs = require('fs');
const path = require('path');
const {mathjax} = require('mathjax-full/js/mathjax.js');
const {TeX} = require('mathjax-full/js/input/tex.js');
const {SVG} = require('mathjax-full/js/output/svg.js');
const {liteAdaptor} = require('mathjax-full/js/adaptors/liteAdaptor.js');
const {RegisterHTMLHandler} = require('mathjax-full/js/handlers/html.js');
const {AllPackages} = require('mathjax-full/js/input/tex/AllPackages.js');

const adaptor = liteAdaptor();
RegisterHTMLHandler(adaptor);
const tex = new TeX({packages: AllPackages});
const svg = new SVG({fontCache: 'local'});
const html = mathjax.document('', {InputJax: tex, OutputJax: svg});

const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const out = process.argv[3];
fs.mkdirSync(out, {recursive: true});
const sizes = {};
for (const [id, f] of Object.entries(spec)) {
  const t = typeof f === 'string' ? f : f.tex;
  const em = (typeof f === 'object' && f.em) || 34;
  const color = (typeof f === 'object' && f.color) || '#1B1B1F';
  const node = html.convert(t, {display: true});
  let s = adaptor.outerHTML(adaptor.firstChild(node));
  const vb = s.match(/viewBox="([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)"/);
  const [minx, miny, w, h] = vb.slice(1).map(Number);
  const k = em / 1000;
  const W = Math.ceil(w * k) + 4, H = Math.ceil(h * k) + 4;
  // inner content of the svg
  const inner = s.replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '');
  let body = inner.replace(/currentColor/g, color).replace(/xlink:href/g, 'href');
  const res = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">` +
    `<g fill="${color}" stroke="${color}" stroke-width="0" transform="translate(${2 - minx * k} ${2 - miny * k}) scale(${k})">${body}</g></svg>`;
  fs.writeFileSync(path.join(out, id + '.svg'), res);
  sizes[id] = [W, H, res.length];
}
fs.writeFileSync(path.join(out, '_sizes.json'), JSON.stringify(sizes, null, 1));
console.log(JSON.stringify(sizes));
