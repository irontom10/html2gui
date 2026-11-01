# html2gtk v0.4.0 — visual pipeline update

The project remains a **C89 GUI library prototype using GTK4**, not a network browser.

## Implemented in the retained canvas runtime

- Native Cairo background gradients and rounded borders (from v0.3.1).
- Gradient-clipped **inline text**: retained HTML spans produce Pango markup and a gradient overlay clipped to the matching UTF-8 range. The Doomsday hero's DOOMSDAY span is detected. This is not the full CSS `background-clip:text` algorithm for every possible writing mode.
- Nested inline formatting via Pango markup: per-span Tailwind foreground, weight, font size and monospace; em/i, u/ins, s/del. UTF-8 markup values are escaped.
- Runtime text measurement via an optional Pango measurement callback; headless layout still uses a rough approximation. Exact CSS line-height and letter spacing are not implemented.
- Tailwind `shadow`, `shadow-sm`, `shadow-md`, `shadow-lg`, `shadow-xl`, `shadow-2xl`, and supported `shadow-COLOR` classes. The Cairo backend approximates soft shadows with layered silhouettes, not a true Gaussian shadow kernel.
- Software-backed `backdrop-blur`, `backdrop-blur-sm`, `-md`, `-lg`, `-xl`, `-2xl`. A bounded box-blur samples only previously rendered pixels in the viewport-sized Cairo image. It is not a GPU blur and may be slow with many overlapping surfaces.
- Flex rows now prefer intrinsic text/button widths, support `flex-1` / `grow` and `justify-center`, `justify-between`, `justify-end`, rather than dividing space equally between all children. This is deliberately **not** a complete CSS Flexbox algorithm. Buttons have reasonable default padding and text alignment.
- Responsive DOM reflow, in-document anchors and a viewport-sized GTK4 drawing surface are retained.

## Build

```
sudo pacman -S --needed base-devel gtk4 pango cairo
make clean
make test
make canvas-browser
./canvas-browser examples/doomsday.html
```

The canvas runtime has a standalone headless core (`h2g_canvas.c`, C89) and a GTK4/Pango/Cairo view (`h2g_browser.c`). `make test` uses no GTK4 headers; `make canvas-browser` requires GTK4. The legacy `html2gtk` GtkBuilder compiler remains available.

## Tests

- Strict ISO C89 with `-pedantic -Werror`: **passed** for the headless core and existing compiler tests.
- Reflow regression: **passed** at 500/650/1100/1600px with one DOM.
- Full 15,223-node Doomsday fixture: **passed** parsing and headless layout.
- New visual command regression: **passed**; 1 gradient text span, 153 rich-text runs, 106 shadow commands, 28 frosted surfaces.
- GTK4 compilation and screenshot comparison: **not tested** in the build container, because GTK4 headers are unavailable.

## Remaining limitations

This is not a standards-compliant browser engine or complete CSS renderer. It still lacks correct general-purpose flex wrapping, Grid auto-placement, line-height/letter spacing fidelity, CSS positioning and stacking contexts, pseudo-elements, arbitrary CSS filters/transforms and native input control integration. The renderer also does not yet expose the final embeddable C89 GUI/widget callback API. The blur is intentionally CPU-only and has a 2.5M-pixel bound.
