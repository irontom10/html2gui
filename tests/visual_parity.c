/* Regression checks for the Doomsday hero / Tailwind v4 parity work.
 * Strict C89. This exercises paint commands, not GTK screenshot pixels. */
#include "../h2g_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int close_to(double x, double y)
{
    double d = x - y;
    return d > -.006 && d < .006;
}

int main(void)
{
    H2GCanvas *page;
    const H2GPaint *p;
    int i, glows = 0, v4_cyan = 0, hero_leading = 0;
    int badges = 0, gradient_text = 0, transparent_gradient = 0;
    int sized_span = 0;
    page = h2g_canvas_new("examples/doomsday.html");
    h2g_canvas_reflow(page, 1600, 900);
    for (i = 0; i < h2g_canvas_count(page); i++) {
        p = h2g_canvas_paint(page, i);
        if (p->type == 7 && p->clip_w > 0 && p->clip_h > 0) {
            glows++;
            if (close_to(p->fg.r, 0.0) &&
                close_to(p->fg.g, 211.0 / 255.0) &&
                close_to(p->fg.b, 242.0 / 255.0) &&
                close_to(p->fg.a, .10)) v4_cyan++;
        }
        if (p->type == 1 && close_to(p->fg.a, .10) &&
            p->w > 60 && p->w < 400 && p->h < 65 &&
            p->fg.r > .7 && p->fg.g < .4) badges++;
        if (p->type == 2 && p->markup &&
            strstr(p->markup, "size=\"7680\"")) sized_span++;
        if (p->type == 2 && p->text &&
            strstr(p->text, "DOOMSDAY") && strstr(p->text, "CLASS TEST")) {
            if (p->markup && strstr(p->markup, "alpha=\"0%\""))
                transparent_gradient++;
            if (close_to(p->line_height, .98)) hero_leading++;
            if (p->gradient_end > p->gradient_start) gradient_text++;
        }
    }
    printf("v0.4.1 paint: glows=%d v4cyan=%d hero-leading=%d gradient-text=%d transparent=%d inline-10px=%d compact-badges=%d\n",
           glows, v4_cyan, hero_leading, gradient_text,
           transparent_gradient, sized_span, badges);
    h2g_canvas_free(page);
    if (glows < 2 || !v4_cyan || !hero_leading || !gradient_text ||
        !transparent_gradient || !sized_span || !badges)
        return 1;
    return 0;
}
