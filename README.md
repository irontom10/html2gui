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

    make clean all test render renderer-test
    ./build/h2g-render examples/hello.html examples/hello.css

To render your own document (head-embedded <style> is supported):

    ./build/h2g-render /path/to/page.html /path/to/style.css

If no font is found automatically, set H2G_FONT to a TrueType path.
Use --headless before the HTML path to create a dummy SDL window, render
one frame, print paint counts and exit. Example:

    ./build/h2g-render --headless examples/hello.html examples/hello.css

The window has wheel/page-up/page-down scrolling and reflows on resize.
Clicking a button prints its HTML id to stdout. Escape closes the window.

## Architecture

1. Lexbor tokenizes HTML and constructs the WHATWG DOM tree.
2. Lexbor parses CSS per the W3C CSS Syntax specification.
3. Lexbor matches supported top-level CSS style selectors against DOM nodes.
4. The initial C89 style engine applies basic properties and a simple cascade.
5. The initial C89 layout engine produces a block/flex approximation.
6. SDL3 draws boxes and *cached* SDL_ttf text textures from a display list.

The public parser API is in include/html2gui/parser.h. The only C99
translation unit is src/parser_lexbor.c, because Lexbor's headers require C99;
all other html2gui code compiles with -std=c89 -pedantic -Werror.

## Rendering scope and caveats

**Currently rendered:** standard DOM text, headings, paragraphs, solid RGB
backgrounds, text colors, margin and padding (one uniform value), font sizes
and weights, simple fixed-width and max-width boxes, basic equal-width flex
rows, buttons with hit testing, scrolling, resize reflow and embedded <style>
plus an optional external CSS file.

**Not implemented:** CSS Grid; real CSS Flexbox and inline formatting
algorithms; block margin collapsing; CSS normal flow; cascade origins,
@media/@supports/@layer handling, custom properties/var(), inline styles;
percentage/calc() lengths; transforms, gradients, shadows, blur, images/SVG,
rounded corners, accessible widget state, focus, IME, keyboard input
widgets, JavaScript or network navigation.

Lexbor *parsing* CSS is not the same as supporting every parsed property
in layout/paint. This renderer is designed as a vertical test slice, not a
claim of complete HTML/CSS compatibility or visual parity with a browser.
On very large documents the per-node selector matching remains expensive.
The bounded display list warns if its command capacity is reached.

## Standards and conformance plan

- [WHATWG HTML Living Standard](https://html.spec.whatwg.org/)
- [W3C CSS Syntax](https://www.w3.org/TR/css-syntax-3/)
- [html5lib-tests](https://github.com/html5lib/html5lib-tests)
- [Web Platform Tests](https://github.com/web-platform-tests/wpt)

This branch replaces the old hand-written HTML/Tailwind-to-GTK implementation.
The previous code remains recoverable from Git history. Tailwind is not
special-cased: it needs a compiled stylesheet, and modern Tailwind features
will not render accurately until the related CSS semantics are implemented.
