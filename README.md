# html2gui

A C89-facing native GUI toolkit project using **standard HTML and CSS**
for interface definitions and **native C callbacks** for application logic.

This is an early SDL3 renderer prototype, not a browser and **not yet a
standards-compliant CSS layout engine**. There is no JavaScript runtime.

## Build on Arch Linux

    sudo pacman -S --needed base-devel cmake pkgconf sdl3 sdl3_ttf ttf-dejavu

Install Lexbor once (it is an external C99 library):

    git clone https://github.com/lexbor/lexbor.git /tmp/lexbor
    cmake -S /tmp/lexbor -B /tmp/lexbor-build -DLEXBOR_BUILD_TESTS=OFF
    cmake --build /tmp/lexbor-build -j8
    sudo cmake --install /tmp/lexbor-build

If Lexbor installs into /usr/local/lib, configure the system linker:

    echo /usr/local/lib | sudo tee /etc/ld.so.conf.d/lexbor.conf
    sudo ldconfig

Build and run:

    make clean all test render renderer-test torture-test
    ./build/h2g-render examples/basic.html examples/basic.css
    ./build/h2g-render examples/torture.html

To render your own document (head-embedded <style> is supported):

    ./build/h2g-render /path/to/page.html /path/to/style.css

If no font is found automatically, set H2G_FONT to a TrueType path.
Use --headless before the HTML path to create a dummy SDL window, render
one frame, print paint counts and exit. Example:

    ./build/h2g-render --headless examples/basic.html examples/basic.css

The window has wheel/page-up/page-down scrolling and reflows on resize.
The CLI demo prints clicked button IDs to stdout; embedders can register a native C callback instead. Escape closes the window.

## Architecture

1. Lexbor tokenizes HTML and constructs the WHATWG DOM tree.
2. Lexbor parses CSS per the W3C CSS Syntax specification.
3. Lexbor matches class-indexed CSS selectors from top-level and nested `@layer` rules against DOM nodes. Supported viewport-width `@media` conditions gate both style rules and media-nested declarations.
4. The initial C89 style engine applies basic properties, root theme variables, and OKLCH colors using an incomplete cascade.
5. The initial C89 layout engine produces a block/flex/grid approximation.
6. SDL3 draws boxes and *cached* SDL_ttf text textures from a display list.

The public parser API is in include/html2gui/parser.h. A reusable
`build/libhtml2gui-renderer.a` also exposes `h2g_renderer_run` in
`include/html2gui/renderer.h`. Pass a plain C function and context pointer to
receive native button clicks by HTML `id`. The function owns the SDL event
loop, does not execute JavaScript, and accepts an optional compiled CSS file.

```c
#include "html2gui/renderer.h"
#include <stdio.h>
#include <string.h>

static void on_button(const char *id, void *user)
{
    (void)user;
    if (strcmp(id, "hello") == 0)
        puts("hello button invoked native C!");
}

int main(void)
{
    return h2g_renderer_run("examples/basic.html", "examples/basic.css",
                            0, on_button, NULL);
}
```

Link with `-lhtml2gui-renderer -lhtml2gui-parser` followed by Lexbor,
SDL3, SDL3_ttf and `-lm`. A complete two-button program is in
`examples/native_callbacks.c`; build and launch it with `make native-example`
then `./build/native-example`. Use `make native-callback-test` for the
headless synthetic click regression test.

The only C99
translation unit is src/parser_lexbor.c, because Lexbor's headers require C99;
all other html2gui code compiles with -std=c89 -pedantic -Werror.

## Rendering scope and caveats

**Currently rendered:** standard DOM text, headings, paragraphs, solid RGB and OKLCH
backgrounds, text colors, margin and padding (one uniform value), font sizes
and weights, simple fixed-width and max-width boxes, basic equal-width flex
rows and grid tracks, `position:absolute` elements kept out of normal flow,
clipped screen-reader-only content, buttons with hit testing, scrolling,
resize reflow and embedded `<style>` plus an optional external CSS file.
Theme variables and a narrow subset of `calc(var(...) * multiplier)` work.
Selected single-width viewport `@media` rules are handled at top level, inside `@layer`, and inside style rules; unsupported media queries are ignored.

**Not implemented:** complete CSS Grid/Flexbox and inline formatting
algorithms; block margin collapsing; full normal flow and positioning;
complete cascade origins/layers and inheritance, general `@media`/`@supports`
queries, general custom-property inheritance/resolution and CSS math;
inline styles, transforms, gradients, shadows, blur, images/SVG, rounded
corners, accessible widget state, focus, IME, keyboard input widgets,
JavaScript or network navigation.

Lexbor *parsing* CSS is not the same as supporting every parsed property
in layout/paint. This renderer is designed as a vertical test slice, not a
claim of complete HTML/CSS compatibility or visual parity with a browser.
The class-indexed selector matcher improves repeated utility-class documents,
but broad selectors still incur work per node. The bounded display list warns
if its command capacity is reached. Run `make torture-test` to render the
3.35 MB Tailwind fixture headlessly and assert non-trivial styling and a
visible hero heading; CI uploads the first-frame BMP for inspection.
This is a stability/style smoke test, **not** a visual parity or standards
conformance certification.

## Standards and conformance plan

- [WHATWG HTML Living Standard](https://html.spec.whatwg.org/)
- [W3C CSS Syntax](https://www.w3.org/TR/css-syntax-3/)
- [html5lib-tests](https://github.com/html5lib/html5lib-tests)
- [Web Platform Tests](https://github.com/web-platform-tests/wpt)

This branch replaces the old hand-written HTML/Tailwind-to-GTK implementation.
The previous code remains recoverable from Git history. Tailwind is not
special-cased: it needs a compiled stylesheet, and modern Tailwind features
will not render accurately until the related CSS semantics are implemented.
