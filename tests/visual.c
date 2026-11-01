/* C89 command-stream assertions for visual Tailwind support. */
#include "../h2g_canvas.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    H2GCanvas *page;
    const H2GPaint *p;
    int i, gradients, borders, heavy, monospace, alpha;
    gradients = borders = heavy = monospace = alpha = 0;
    page = h2g_canvas_new("examples/doomsday.html");
    h2g_canvas_reflow(page, 1600, 900);
    for (i = 0; i < h2g_canvas_count(page); i++) {
        p = h2g_canvas_paint(page, i);
        if (p->type == 3 && p->w > 100 && p->h > 100) gradients++;
        if (p->type == 4 && p->w > 100 && p->h > 100) borders++;
        if (p->type == 2 && p->weight == 900) heavy++;
        if (p->type == 2 && p->monospace) monospace++;
        if (p->type == 4 && p->fg.a < 1) alpha++;
    }
    if (!gradients || borders < 10 || !heavy || !monospace || !alpha) {
        fprintf(stderr, "FAIL gradients=%d borders=%d heavy=%d mono=%d alpha=%d\n",
                gradients, borders, heavy, monospace, alpha);
        h2g_canvas_free(page);
        return 1;
    }
    printf("PASS visual commands: gradients=%d borders=%d heavy=%d mono=%d alpha-border=%d\n",
           gradients, borders, heavy, monospace, alpha);
    h2g_canvas_free(page);
    return 0;
}
