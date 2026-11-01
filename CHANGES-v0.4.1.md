# v0.4.1 — screenshot-driven visual parity

Compared the C89/GTK4 canvas prototype with the actual browser rendering of
`examples/doomsday.html` at similar desktop widths. Specific differences
included an oversized warning badge, the incorrect v3 Tailwind palette,
wrong placement of the gradient's colored stops, no decorative blurred glows,
font spacing drift, and artificial shadow contours.

## File-level changes

- `h2g_palette_v4.h`: full 22-family, 11-shade palette (242 entries) derived
  from the embedded Tailwind 4.1.10 theme's OKLCH tokens. No runtime color
  conversion dependency.
- `h2g_canvas.c/.h`: v4 palette lookup, `inline-flex` shrink-to-fit,
  `sr-only`, spacing and tracking utilities, inherited absolute line height,
  recursive inline markup with font-size and tracking conversions,
  transparency for clipped gradient text, Pango-assisted natural widths,
  and clipped glow paint commands for decorative absolute blobs.
- `h2g_browser.c`: span-local gradient stops, clipped radial glow painting,
  3-pass software shadow masks, and Pango line spacing in both layout
  measurement and painting. The viewport-sized drawing surface is retained.
- `tests/visual_parity.c`: exact regression for the hero issues above.
- `Makefile`: integrates parity test into `make test` and `make canvas-test`.

## Validation performed

- `make test`: PASS, strict C89 headless core, existing tests, and parity checks.
- DOM reflow 1600 -> 500 -> 1100 -> 650 -> 1600: PASS.
- Full Doomsday fixture at 650 and 1600px: PASS.
- GTK4 frontend compile/launch and screenshot comparison: **not performed**.

## Limitations

Only a *visual parity improvement* for the demonstrated page, not complete
HTML/CSS/Tailwind support. GTK4 remains the window/input integration layer.
Pango and Cairo do text/painting. The final C89 app/widget API is future work.
