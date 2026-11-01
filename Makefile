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

$(BUILD)/h2g_sdl.o: src/h2g_sdl.c src/h2g_text_input.h include/html2gui/parser.h include/html2gui/renderer.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) -Isrc -c $< -o $@

$(BUILD)/h2g_text_input.o: src/h2g_text_input.c src/h2g_text_input.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(C89_WARN) -Isrc -c $< -o $@

$(BUILD)/libhtml2gui-renderer.a: $(BUILD)/h2g_sdl.o $(BUILD)/h2g_text_input.o
	$(AR) rcs $@ $^

$(BUILD)/h2g-render: tools/h2g_render.c $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) $(SDL_LIBS) -lm -o $@

$(BUILD)/native-callback-smoke: tests/native_callback_smoke.c $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) $(SDL_LIBS) -lm -o $@

$(BUILD)/native-example: examples/native_callbacks.c $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) $(SDL_LIBS) -lm -o $@

render: $(BUILD)/h2g-render

$(BUILD)/text-input-smoke: tests/text_input_smoke.c $(BUILD)/h2g_text_input.o
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(C89_WARN) -Isrc $< $(BUILD)/h2g_text_input.o -o $@

$(BUILD)/native-input-smoke: tests/native_input_smoke.c $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(CFLAGS) $(C89_WARN) $< $(BUILD)/libhtml2gui-renderer.a $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) $(SDL_LIBS) -lm -o $@

test: $(BUILD)/parser-smoke $(BUILD)/text-input-smoke
	./$(BUILD)/parser-smoke
	./$(BUILD)/text-input-smoke

renderer-test: render
	./$(BUILD)/h2g-render --headless examples/basic.html examples/basic.css

.PHONY: native-callback-test
native-callback-test: $(BUILD)/native-callback-smoke
	H2G_TEST_CLICK=hello ./$(BUILD)/native-callback-smoke

.PHONY: native-input-test
native-input-test: $(BUILD)/native-input-smoke
	H2G_TEST_INPUT=name H2G_TEST_TEXT=abc ./$(BUILD)/native-input-smoke

.PHONY: native-input-click-test
native-input-click-test: $(BUILD)/native-input-smoke
	H2G_TEST_INPUT=name H2G_TEST_TEXT=abc H2G_TEST_INPUT_X=0.01 ./$(BUILD)/native-input-smoke

.PHONY: native-input-viewport-test
native-input-viewport-test: render
	H2G_TEST_INPUT_VIEW=1 ./$(BUILD)/h2g-render --headless examples/basic.html examples/basic.css

.PHONY: native-example
native-example: $(BUILD)/native-example

.PHONY: torture-test
torture-test: render
	timeout 90s env H2G_TORTURE_VERIFY=1 H2G_DUMP_LAYOUT=1 H2G_SCREENSHOT=$(BUILD)/torture.bmp ./$(BUILD)/h2g-render --headless examples/torture.html

clean:
	rm -rf $(BUILD)
