#ifndef H2G_CANVAS_H
#define H2G_CANVAS_H

/*
 * Experimental retained DOM / responsive box layout / paint command core.
 * ISO C89. Backend independent, no GTK or browser engine dependencies.
 * Origin (0,0) is the top-left of the document, sizes are CSS pixels.
 */

typedef struct H2GColor {
    double r, g, b, a;
} H2GColor;

typedef struct H2GPaint {
    int type;                  /* 1: solid box, 2: text, 3: linear gradient, 4: border, 5: shadow, 6: backdrop blur */
    int node;
    double x, y, w, h;
    double radius;
    H2GColor fg;
    H2GColor via, end;
    int border_width;
    int gradient_direction; /* 1:right, 2:bottom-right, 3:bottom */
    int weight;
    int monospace;
    char *text;               /* owned by core, valid until next reflow */
    int font_size;
    int bold;
    int align;               /* Pango alignment: 0 left, 1 center, 2 right */
    char *markup;            /* Pango markup for formatted text */
    int gradient_start, gradient_end; /* UTF-8 byte range in unescaped text */
    H2GColor text_from, text_via, text_to;
    int text_has_via;
    double shadow_blur;
    double line_height, tracking;
    double clip_x, clip_y, clip_w, clip_h; /* radial glow clipping */
} H2GPaint;

typedef struct H2GRect {
    double x, y, w, h;
    int visible;
} H2GRect;

typedef struct H2GCanvas H2GCanvas;
/* Optional backend text metrics; headless layout retains approximate fallback. */
typedef int (*H2GTextMeasure)(const char *markup, int font_size, int weight,
                              int monospace, double width, double line_height,
                              void *user_data, int *natural_width);
void h2g_canvas_set_text_measure(H2GCanvas *canvas, H2GTextMeasure measure,
                                 void *user_data);

H2GCanvas *h2g_canvas_new(const char *filename);
void h2g_canvas_free(H2GCanvas *canvas);
void h2g_canvas_reflow(H2GCanvas *canvas, int width, int height);
int h2g_canvas_count(const H2GCanvas *canvas);
const H2GPaint *h2g_canvas_paint(const H2GCanvas *canvas, int index);
H2GRect h2g_canvas_id_rect(const H2GCanvas *canvas, const char *id);
int h2g_canvas_node_count(const H2GCanvas *canvas);
int h2g_canvas_document_height(const H2GCanvas *canvas);
int h2g_canvas_columns(const H2GCanvas *canvas, const char *id);
/* Return anchor target node id for a click, or NULL. */
const char *h2g_canvas_hit_href(const H2GCanvas *canvas, double x, double y);

#endif
