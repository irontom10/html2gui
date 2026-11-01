CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CPPFLAGS ?=
# All html2gui-owned code is strict C89. Lexbor itself is compiled separately.
WARN = -std=c89 -pedantic -Wall -Wextra -Werror
LEXBOR_CFLAGS ?= $(shell pkg-config --cflags lexbor 2>/dev/null)
LEXBOR_LIBS ?= $(shell pkg-config --libs lexbor 2>/dev/null || echo -llexbor)
INCLUDES = -Iinclude $(LEXBOR_CFLAGS)
BUILD = build

.PHONY: all test clean
all: $(BUILD)/libhtml2gui-parser.a $(BUILD)/h2g-parse

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/parser_lexbor.o: src/parser_lexbor.c include/html2gui/parser.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/libhtml2gui-parser.a: $(BUILD)/parser_lexbor.o
	$(AR) rcs $@ $<

$(BUILD)/h2g-parse: tools/h2g_parse.c $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARN) $< $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) -o $@

$(BUILD)/parser-smoke: tests/parser_smoke.c $(BUILD)/libhtml2gui-parser.a
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARN) $< $(BUILD)/libhtml2gui-parser.a $(LEXBOR_LIBS) -o $@

test: $(BUILD)/parser-smoke
	./$(BUILD)/parser-smoke

clean:
	rm -rf $(BUILD)
