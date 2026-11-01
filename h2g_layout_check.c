/* Headless regression/test harness. Strict C89 and no GUI dependency. */
#include "h2g_canvas.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    H2GCanvas *canvas;
    H2GRect r;
    int width, i;
    if (argc < 3) {
        fprintf(stderr, "usage: %s page.html width [id ...]\n", argv[0]);
        return 2;
    }
    width = atoi(argv[2]);
    if (width < 240 || width > 10000) return 2;
    canvas = h2g_canvas_new(argv[1]);
    h2g_canvas_reflow(canvas, width, 900);
    printf("viewport=%d nodes=%d draws=%d document_height=%d\n",
           width, h2g_canvas_node_count(canvas), h2g_canvas_count(canvas),
           h2g_canvas_document_height(canvas));
    for (i = 3; i < argc; i++) {
        r = h2g_canvas_id_rect(canvas, argv[i]);
        printf("id=%s visible=%d x=%.0f y=%.0f w=%.0f h=%.0f cols=%d\n",
               argv[i], r.visible, r.x, r.y, r.w, r.h,
               h2g_canvas_columns(canvas, argv[i]));
    }
    h2g_canvas_free(canvas);
    return 0;
}
