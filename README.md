# html2gtk v0.4.1 — C89 GUI rendering prototype

**Purpose:** HTML and Tailwind as declarative *native C GUI* markup, with a
GTK4/Pango/Cairo backend. This is **not** intended to become a network browser.
The C89 canvas runtime and older GtkBuilder generator are separate backends.

## Build and preview on Arch

```sh
sudo pacman -S --needed base-devel gtk4 pango cairo
make clean
make test
make canvas-browser
./canvas-browser examples/doomsday.html
```

Compare the canvas at the **same logical window width** as the HTML reference,
then resize it. At 650px, the sidebar is hidden and metrics stack into two
columns; at 1600px, the sidebar is 300px and metrics use four columns.

The older static GtkBuilder compiler is retained:

```sh
make preview
./html2gtk examples/errs.html output.ui output.css ERRS 1600
./preview output.ui output.css
```

## Visual parity improvements in v0.4.1

- **Tailwind CSS v4 colors:** `h2g_palette_v4.h` holds 242 OKLCH theme colors,
  converted to sRGB. The canvas backend uses these rather than the older
  compiler's Tailwind v3 table. Opacity suffixes (`/10`, `/20`, etc.) survive.
- **Correct gradient text placement:** Gradient stops are computed over the
  styled text span's Pango x-range, not over the entire enclosing label.
  The base gradient text is transparent so the gradient is not double-painted.
- **Decorative glows:** `absolute` blurred cyan/fuchsia blobs in the Doomsday
  hero no longer take up layout space. They are rendered as clipped radial
  glows. This approximates the CSS blurred element, not arbitrary CSS filter
  behavior.
- **Line height and tracking:** `leading-*`, `leading-[.98]`, `tracking-*`,
  and `tracking-[.25em]` are translated for text. Text with nested Pango spans
  now uses px-to-pt conversion for per-span font sizes and letter spacing.
- **Inline-flex fit-content:** The warning badge/pill now uses intrinsic
  content width, rather than stretching across the panel.
- **Natural flex text sizes:** Where Pango is available, controls get measured
  glyph widths; plain C headless tests keep an approximate fallback.
- **Soft shadows:** The Cairo frontend generates bounded blurred A8 masks
  (three box filter passes) rather than eight concentric silhouette layers.
  Frosted backdrop blur remains software based.
- **Paint ordering:** Decorative content is drawn underneath rounded borders;
  `sr-only` content is omitted from visible layout. Flex link text children
  have a visible inter-element gap.

## Verification

`make test` compiles the core with strict ISO C89 (`-std=c89 -pedantic`), runs
the previous parser/compiler and responsive layout tests, and adds
`h2g-parity-test`: two hero glow paints, a v4 cyan color match, `.98` hero
leading, clipped gradient text, transparent gradient markup, converted
10px inline markup, and compact inline badge geometry.

**GTK caveat:** GTK4/Pango/Cairo development headers are not installed in the
artifact build environment. The `canvas-browser` target could not be compiled
or screenshot-tested here. Run it on Bertha; this release is a source-level
and headless-command-stream fix, not a claim of pixel-perfect browser parity.

## Architectural limits

- HTML5 parsing, CSS Flexbox/Grid, selectors and responsive media behavior are
  **partial implementations**, not standards-complete.
- The source retains the legacy shared-parser include until a dedicated DOM
  translation unit is extracted.
- Sticky positioning, browser overflow/stacking semantics, image/video/SVG
  rendering, nested transforms, complete CSS filters, hover/focus state
  machines, and native input widget callbacks remain incomplete.
- Pango line spacing is an approximation of browser CSS line boxes. Inline
  markup has color/weight/style/size but not a complete CSS inline formatting
  context.
- Shadows and frosted panels are software effects; large pages can incur
  significant CPU work while scrolling or resizing.

The goal remains: a **small, embeddable C89 GUI library on GTK4** with HTML and
Tailwind as its UI format and application logic in C.

Previous work: `CHANGES-v0.4.md`. Current release: `CHANGES-v0.4.1.md`.
