# html2gui

Native C GUI toolkit project using **standard HTML and CSS** for interface
definitions and **C89** for application logic.

## Standards parser bootstrap

This branch deliberately **replaces the old hand-written HTML/Tailwind parser**.
HTML tokenization, error recovery, tree construction, named character references,
foreign content and DOM come from [Lexbor](https://github.com/lexbor/lexbor),
which targets the [WHATWG HTML Living Standard](https://html.spec.whatwg.org/).

CSS tokenization, at-rules, qualified rules, selectors and declaration parsing
come from Lexbor's CSS modules, based on the
[W3C CSS Syntax Module](https://www.w3.org/TR/css-syntax-3/).
**Do not special-case Tailwind tokens in the HTML parser.** Tailwind becomes
ordinary CSS input when compiled to a stylesheet.

Lexbor itself is implemented in **C99**. Its public headers use C99 inline,
so the isolated src/parser_lexbor.c adapter is compiled as C99, although its
own code uses C89 syntax. **The published html2gui header and all application
and test code compile under strict ISO C89.** The dependency is not fully C89;
a no-C99-dependencies policy would require a different implementation.

### Build

Install [Lexbor](https://lexbor.com/documentation/) headers and library
(e.g. build and install Lexbor 2.9+ using CMake), and a C compiler:

    make
    make test
    build/h2g-parse html some-file.html
    build/h2g-parse css some-file.css

The parser API is in [include/html2gui/parser.h](include/html2gui/parser.h).

### Scope of this change

- WHATWG document parsing through Lexbor, DOM traversal and element names.
- W3C CSS stylesheet parsing through Lexbor, traversable top-level rule chain.
- An independent C89-facing API and smoke tests.
- No GTK, SDL, Cairo, Tailwind-specific parser or custom HTML tokenizer.

**Not implemented yet:** computed style / cascade, selectors applied to DOM,
CSSOM-to-style mapping, layout (block/inline/flex/grid), painting, SDL backend,
native event handling, widget state and accessibility. Parsing valid CSS does
not mean the toolkit renders all CSS. Likewise HTML DOM parsing is not all
of HTML's interactive/browser behavior.

The old v0.4.x implementation remains recoverable from Git history. The
new tree is intentionally small and not backwards-compatible.

### Next milestones

1. Expose DOM attributes, text, namespaces, CSS rule/declaration traversal.
2. Connect selector matching and CSS cascade, including origins, layers,
   inheritance, custom properties and media queries.
3. Implement layout primitives and reference test against browser fixtures.
4. Implement SDL3 rendering, text input and C widget event bindings.

### Compliance tracking

Use [html5lib-tests](https://github.com/html5lib/html5lib-tests) for HTML
tokenizer/tree-builder conformance and [web-platform-tests](https://github.com/web-platform-tests/wpt)
for CSS Syntax and rendering behavior. The current smoke tests prove API
wiring only; they are not a substitute for a conformance report.
