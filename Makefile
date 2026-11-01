CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CPPFLAGS ?=
C89_WARN = -std=c89 -pedantic -Wall -Wextra -Werror
# Lexbor headers require C99; all public and SDL renderer code is C89.
BRIDGE_WARN = -std=c99 -Wall -Wextra -Werror
LEXBOR_CFLAGS ?= $(shell pkg-config --cflags lexbor 2>/dev/null)
LEXBOR_LIBS ?= $(shell pkg-config --libs lexbor 2>/dev/null || echo -llexbor)
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl3 sdl3-ttf 2>/dev/null)
SDL_LIBS ?= $(shell pkg-config --libs sdl3 sdl3-ttf 2>/dev/null)
INCLUDES = -Iinclude $(LEXBOR_CFLAGS)
BUILD = build

.PHONY: all render test renderer-test clean
all: $(BUILD)/libhtml2gui-parser.a $(BUILD)/h2g-parse

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/parser_lexbor.o: src/parser_lexbor.c include/html2gui/parser.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(BRIDGE_WARN) -c $< -o $@

$(BUILD)/libhtml2gui-parser.a: $(BUILD)/parser_lexbor.o
	$(AR) rcs $@ $<

$(BUILD)/h2g-parse: tools/h2g_parse.c $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) -o $@

$(BUILD)/parser-smoke: tests/parser_smoke.c $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) -o $@

$(BUILD)/h2g-render: src/h2g_sdl.c include/html2gui/parser.h $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) src/h2g_sdl.c $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) $(SDL_LIBS) -lm -o $@

render: $(BUILD)/h2g-render

test: $(BUILD)/parser-smoke
	./$(BUILD)/parser-smoke

renderer-test: render
	./$(BUILD)/h2g-render --headless examples/basic.html examples/basic.css

.PHONY: torture-test
torture-test: render
	timeout 90s ./$(BUILD)/h2g-render --headless examples/torture.html

clean:
	rm -rf $(BUILD)
