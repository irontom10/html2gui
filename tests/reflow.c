/* Test repeated reflow of SAME parsed DOM, not independent recompilations. */
#include "../h2g_canvas.h"
#include <stdio.h>
#include <stdlib.h>

static void check(H2GCanvas *page, int width, int sidebar, int col, int cols)
{
    H2GRect r = h2g_canvas_id_rect(page, "side");
    H2GRect m = h2g_canvas_id_rect(page, "content");
    int n = h2g_canvas_columns(page, "metrics");
    if (r.visible != sidebar || (int)m.x != col || n != cols) {
        fprintf(stderr, "FAIL: width=%d side=%d x=%d cols=%d (expected %d %d %d)\n",
                width, r.visible, (int)m.x, n, sidebar, col, cols);
        exit(1);
    }
    if (h2g_canvas_document_height(page) <= 0 ||
        h2g_canvas_count(page) < 4) exit(1);
}

int main(void)
{
    H2GCanvas *page = h2g_canvas_new("tests/responsive.html");
    h2g_canvas_reflow(page, 1600, 900); check(page, 1600, 1, 300, 4);
    h2g_canvas_reflow(page, 500, 900); check(page, 500, 0, 0, 1);
    h2g_canvas_reflow(page, 1100, 900); check(page, 1100, 1, 270, 2);
    h2g_canvas_reflow(page, 650, 900); check(page, 650, 0, 0, 2);
    h2g_canvas_reflow(page, 1600, 600); check(page, 1600, 1, 300, 4);
    h2g_canvas_free(page);
    puts("PASS: one DOM reflowed 1600 -> 500 -> 1100 -> 650 -> 1600; breakpoint state and geometry correct");
    return 0;
}
