# PolyFEA research deck — sources

Slide sources for the PolyFEA research / application deck (35 slides, 1920×1080).
The live deck is a claude.ai Slides artifact; these files are its content.

- `deck.json`, `slides/*.html` — deck index and one file per slide (Slides artifact format).
  Images are referenced as `/_blob/<id>`; `tools/blobs.txt` maps each id to its file in `assets/`.
- `assets/art` — generated vector illustrations (title band, meshing pipeline, Tet4/Tet10, model graph).
- `assets/fx`, `assets/fx2` — formulas rendered from LaTeX with MathJax (`tools/formulas.json`, `tools/f2.json`).
- `assets/img` — screenshots and validation-plan figures.
- `tools/` — `gen_art.py`, `gen_graph.py`, `tex2svg.js` (needs `mathjax-full@3.2.2`), and `preview.js` (local Playwright preview).
