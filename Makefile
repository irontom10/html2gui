CC ?= cc
CFLAGS ?= -O3

.PHONY: all preview canvas-browser sample test canvas-test clean
all: html2gtk h2g-layout-check

html2gtk: html2gtk.c
	$(CC) $(CFLAGS) -o $@ $<

h2g-layout-check: h2g_canvas.c h2g_canvas.h html2gtk.c h2g_layout_check.c
	$(CC) $(CFLAGS) -Werror -o $@ h2g_canvas.c h2g_layout_check.c -lm

h2g-parity-test: h2g_canvas.c h2g_canvas.h h2g_palette_v4.h tests/visual_parity.c
	$(CC) $(CFLAGS) -Werror -o $@ h2g_canvas.c tests/visual_parity.c -lm

h2g-v4-test: h2g_canvas.c h2g_canvas.h tests/visual_v4.c
	$(CC) $(CFLAGS) -Werror -o $@ h2g_canvas.c tests/visual_v4.c -lm

h2g-visual-test: h2g_canvas.c h2g_canvas.h html2gtk.c tests/visual.c
	$(CC) $(CFLAGS) -Werror -o $@ h2g_canvas.c tests/visual.c -lm

h2g-reflow-test: h2g_canvas.c h2g_canvas.h html2gtk.c tests/reflow.c
	$(CC) $(CFLAGS) -Werror -o $@ h2g_canvas.c tests/reflow.c -lm

preview: preview.c h2g_layout.c h2g_layout.h
	$(CC) $(CFLAGS) -o $@ preview.c h2g_layout.c $$(pkg-config --cflags --libs gtk4)

canvas-browser: h2g_browser.c h2g_canvas.c h2g_canvas.h html2gtk.c
	$(CC) $(CFLAGS) -o $@ h2g_browser.c h2g_canvas.c $$(pkg-config --cflags --libs gtk4 pangocairo) -lm

sample: html2gtk
	./html2gtk examples/errs.html examples/errs.ui examples/errs.css 'ERRS Diag'

test: all h2g-reflow-test h2g-visual-test h2g-v4-test h2g-parity-test
	sh tests/test.sh
	sh tests/test-runtime.sh
	./h2g-visual-test
	./h2g-v4-test
	./h2g-parity-test

canvas-test: h2g-layout-check h2g-reflow-test h2g-visual-test h2g-v4-test h2g-parity-test
	sh tests/test-runtime.sh
	./h2g-visual-test
	./h2g-v4-test
	./h2g-parity-test

clean:
	rm -f html2gtk preview canvas-browser h2g-layout-check h2g-reflow-test h2g-visual-test h2g-v4-test h2g-parity-test examples/errs.ui examples/errs.css
