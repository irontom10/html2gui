/* Visual pipeline regression: Doomsday fixture is the actual test input. */
#include "../h2g_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    H2GCanvas *page;
    const H2GPaint *p;
    int i, gradient_text = 0, rich_text = 0, shadow = 0, frost = 0;
    int btn = 0;
    page = h2g_canvas_new("examples/doomsday.html");
    h2g_canvas_reflow(page, 1600, 900);
    for (i = 0; i < h2g_canvas_count(page); i++) {
        p = h2g_canvas_paint(page, i);
        if (p->type == 5 && p->shadow_blur > 0) shadow++;
        if (p->type == 6 && p->shadow_blur > 0) frost++;
        if (p->type == 2 && p->markup && strstr(p->markup, "<span")) rich_text++;
        if (p->type == 2 && p->gradient_end > p->gradient_start &&
            p->gradient_start >= 0) gradient_text++;
        if (p->type == 2 && p->text && strstr(p->text, "Enter the gauntlet") &&
            p->w > 0) btn++;
    }
    printf("v0.4 visual: gradient-text=%d rich-text=%d shadow=%d frost=%d buttons=%d\n",
           gradient_text, rich_text, shadow, frost, btn);
    if (!gradient_text || !rich_text || shadow < 10 || frost < 2 || !btn) {
        h2g_canvas_free(page); return 1;
    }
    h2g_canvas_free(page);
    return 0;
}
