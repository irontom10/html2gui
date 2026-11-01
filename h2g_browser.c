/*
 * html2gtk experimental GTK4 canvas browser, MIT.
 * Strict C89. HTML parsed once; layout recomputed when viewport changes.
 * One viewport-sized GtkDrawingArea; native GTK scrollbar; Cairo/Pango paint.
 * No WebKit, JS runtime, C++, or nested widget-per-DOM-element design.
 *
 * INCOMPLETE: only a subset of block/flex/grid, typography and backgrounds.
 * Text measurement is approximate during layout; painting uses Pango.
 */
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "h2g_canvas.h"

#define H2G_PI 3.14159265358979323846

typedef struct Browser {
    H2GCanvas *document;
    GtkWidget *window;
    GtkWidget *surface;
    GtkAdjustment *adjustment;
    GMainLoop *loop;
    int last_width;
    int last_height;
} Browser;

static void browser_rounded(cairo_t *cr, double x, double y,
                            double w, double h, double r)
{
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -H2G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, H2G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, H2G_PI / 2, H2G_PI);
    cairo_arc(cr, x + r, y + r, r, H2G_PI, 3 * H2G_PI / 2);
    cairo_close_path(cr);
}

/* Gradient text uses Pango glyph paths, restricted to the styled UTF-8
 * byte range. The same layout determines glyph positions and wrapping. */
static void browser_gradient_text(cairo_t *cr, PangoLayout *layout,
                                  const H2GPaint *p)
{
    PangoLayoutIter *iter;
    PangoLayoutLine *line;
    PangoRectangle logical;
    int *ranges = NULL, nr = 0, i;
    cairo_pattern_t *gradient;
    double x2, min_x = p->x + p->w, max_x = p->x;
    if (p->gradient_end <= p->gradient_start) return;
    cairo_save(cr);
    cairo_new_path(cr);
    iter = pango_layout_get_iter(layout);
    do {
        line = pango_layout_iter_get_line_readonly(iter);
        pango_layout_iter_get_line_extents(iter, NULL, &logical);
        if (line) {
            ranges = NULL; nr = 0;
            pango_layout_line_get_x_ranges(line, p->gradient_start,
                            p->gradient_end, &ranges, &nr);
            for (i = 0; i < nr; i++) {
                double xx = p->x + (double)ranges[i * 2] / PANGO_SCALE;
                double ww = (double)(ranges[i * 2 + 1] - ranges[i * 2]) / PANGO_SCALE;
                if (ww > 0) {
                    if (xx < min_x) min_x = xx;
                    if (xx + ww > max_x) max_x = xx + ww;
                    cairo_rectangle(cr, xx,
                      p->y + (double)logical.y / PANGO_SCALE,
                      ww, (double)logical.height / PANGO_SCALE);
                }
            }
            g_free(ranges);
        }
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
    if (max_x <= min_x) { cairo_restore(cr); return; }
    cairo_clip(cr);
    x2 = max_x;
    gradient = cairo_pattern_create_linear(min_x, p->y, x2, p->y);
    cairo_pattern_add_color_stop_rgba(gradient, 0, p->text_from.r,
                      p->text_from.g, p->text_from.b, p->text_from.a);
    if (p->text_has_via)
        cairo_pattern_add_color_stop_rgba(gradient, .5, p->text_via.r,
                        p->text_via.g, p->text_via.b, p->text_via.a);
    cairo_pattern_add_color_stop_rgba(gradient, 1, p->text_to.r,
                          p->text_to.g, p->text_to.b, p->text_to.a);
    cairo_move_to(cr, p->x, p->y);
    pango_cairo_layout_path(cr, layout);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);
    cairo_restore(cr);
}

/* A radial paint approximation of large blurred decorative HTML blobs.
 * The rect clips to the containing element, avoiding a full-screen blur. */
static void browser_glow(cairo_t *cr, const H2GPaint *p)
{
    cairo_pattern_t *pattern;
    double x, y, rx, ry, rad;
    if (p->clip_h <= 0 || p->clip_w <= 0 || p->w <= 0 || p->h <= 0)
        return;
    cairo_save(cr);
    browser_rounded(cr, p->clip_x, p->clip_y, p->clip_w,
                    p->clip_h, p->radius);
    cairo_clip(cr);
    x = p->x + p->w / 2.0;
    y = p->y + p->h / 2.0;
    rx = p->w / 2.0;
    ry = p->h / 2.0;
    rad = rx > ry ? rx : ry;
    rad += p->shadow_blur * .80;
    pattern = cairo_pattern_create_radial(x, y, 0, x, y, rad);
    cairo_pattern_add_color_stop_rgba(pattern, 0,
           p->fg.r, p->fg.g, p->fg.b, p->fg.a * .78);
    cairo_pattern_add_color_stop_rgba(pattern, .45,
           p->fg.r, p->fg.g, p->fg.b, p->fg.a * .53);
    cairo_pattern_add_color_stop_rgba(pattern, 1,
           p->fg.r, p->fg.g, p->fg.b, 0);
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, p->clip_x, p->clip_y, p->clip_w, p->clip_h);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
    cairo_restore(cr);
}

/* Three box-filter passes approximate a Gaussian kernel with a bounded
 * A8 mask. This avoids the visible concentric outlines of the old eight
 * rectangle shadow. No GPU allocation and no browser CSS machinery. */
static void browser_blur_a8(const unsigned char *src, unsigned char *dst,
                            int w, int h, int radius, int vertical)
{
    int outer, inner, end_outer, end_inner, sum, enter, leave;
    int stride = vertical ? w : 1;
    if (radius < 1) {
        memcpy(dst, src, (size_t)w * h);
        return;
    }
    end_outer = vertical ? w : h;
    end_inner = vertical ? h : w;
    for (outer = 0; outer < end_outer; outer++) {
        int base = vertical ? outer : outer * w;
        sum = 0;
        for (inner = 0; inner <= radius && inner < end_inner; inner++)
            sum += src[base + inner * stride];
        for (inner = 0; inner < end_inner; inner++) {
            dst[base + inner * stride] =
                (unsigned char)(sum / (2 * radius + 1));
            enter = inner + radius + 1;
            leave = inner - radius;
            if (enter < end_inner) sum += src[base + enter * stride];
            if (leave >= 0) sum -= src[base + leave * stride];
        }
    }
}

static void browser_shadow(cairo_t *cr, const H2GPaint *p)
{
    cairo_surface_t *mask;
    cairo_t *mask_cr;
    unsigned char *a, *b, *data;
    int pad, w, h, y, radius, stride, i;
    double dy;
    if (p->shadow_blur < 1 || p->fg.a <= 0 || p->w < 1 || p->h < 1)
        return;
    radius = (int)(p->shadow_blur / 4.0 + .5);
    if (radius < 1) radius = 1;
    pad = 3 * radius * 2 + 3;
    w = (int)ceil(p->w) + pad * 2;
    h = (int)ceil(p->h) + pad * 2;
    if (w <= 0 || h <= 0 || (double)w * h > 3000000) return;
    mask = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(mask);
        return;
    }
    mask_cr = cairo_create(mask);
    dy = p->shadow_blur * .15;
    browser_rounded(mask_cr, pad, pad + dy, p->w, p->h, p->radius);
    cairo_set_source_rgba(mask_cr, 1, 1, 1, 1);
    cairo_fill(mask_cr);
    cairo_destroy(mask_cr);
    cairo_surface_flush(mask);
    stride = cairo_image_surface_get_stride(mask);
    data = cairo_image_surface_get_data(mask);
    a = (unsigned char *)g_malloc((size_t)w * h);
    b = (unsigned char *)g_malloc((size_t)w * h);
    for (y = 0; y < h; y++)
        memcpy(a + (size_t)y * w, data + (size_t)y * stride, (size_t)w);
    for (i = 0; i < 3; i++) {
        browser_blur_a8(a, b, w, h, radius, 0);
        browser_blur_a8(b, a, w, h, radius, 1);
    }
    for (y = 0; y < h; y++)
        memcpy(data + (size_t)y * stride, a + (size_t)y * w, (size_t)w);
    cairo_surface_mark_dirty(mask);
    g_free(b); g_free(a);
    cairo_save(cr);
    cairo_set_source_rgba(cr, p->fg.r, p->fg.g, p->fg.b, p->fg.a);
    cairo_mask_surface(cr, mask, p->x - pad, p->y - pad);
    cairo_restore(cr);
    cairo_surface_destroy(mask);
}

/* Apply a separable box blur to pixels already painted under a
 * translucent panel. This is a bounded, software Cairo fallback: GTK4/GSK
 * can replace it with a GPU blur in a later backend. */
static void browser_frost(cairo_t *cr, const H2GPaint *p, double offset)
{
    cairo_surface_t *target, *blurred;
    unsigned char *data, *pixels, *temp, *dst;
    int ix, iy, w, h, r, stride, b, xx, yy, c, k, start, end;
    int sum;
    double sx = p->x, sy = p->y - offset;
    int canvas_w, canvas_h;
    target = cairo_get_target(cr);
    if (cairo_surface_get_type(target) != CAIRO_SURFACE_TYPE_IMAGE) return;
    canvas_w = cairo_image_surface_get_width(target);
    canvas_h = cairo_image_surface_get_height(target);
    ix = (int)floor(sx); iy = (int)floor(sy);
    w = (int)ceil(sx + p->w) - ix;
    h = (int)ceil(sy + p->h) - iy;
    if (ix < 0) { w += ix; ix = 0; }
    if (iy < 0) { h += iy; iy = 0; }
    if (ix + w > canvas_w) w = canvas_w - ix;
    if (iy + h > canvas_h) h = canvas_h - iy;
    if (w < 2 || h < 2 || (double)w * h > 2500000) return;
    r = (int)(p->shadow_blur / 2);
    if (r < 1) r = 1;
    if (r > 20) r = 20;
    stride = cairo_image_surface_get_stride(target);
    cairo_surface_flush(target);
    data = cairo_image_surface_get_data(target);
    pixels = (unsigned char *)g_malloc((size_t)w * h * 4);
    temp = (unsigned char *)g_malloc((size_t)w * h * 4);
    for (yy = 0; yy < h; yy++)
        memcpy(pixels + (size_t)yy * w * 4,
               data + (size_t)(yy + iy) * stride + (size_t)ix * 4,
               (size_t)w * 4);
    for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++) {
        start = xx - r; if (start < 0) start = 0;
        end = xx + r; if (end >= w) end = w - 1;
        for (c = 0; c < 4; c++) {
            sum = 0;
            for (k = start; k <= end; k++)
                sum += pixels[((size_t)yy * w + k) * 4 + c];
            temp[((size_t)yy * w + xx) * 4 + c] =
                         (unsigned char)(sum / (end - start + 1));
        }
    }
    for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++) {
        start = yy - r; if (start < 0) start = 0;
        end = yy + r; if (end >= h) end = h - 1;
        for (c = 0; c < 4; c++) {
            sum = 0;
            for (k = start; k <= end; k++)
                sum += temp[((size_t)k * w + xx) * 4 + c];
            pixels[((size_t)yy * w + xx) * 4 + c] =
                         (unsigned char)(sum / (end - start + 1));
        }
    }
    blurred = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    dst = cairo_image_surface_get_data(blurred);
    for (yy = 0; yy < h; yy++)
        memcpy(dst + (size_t)yy * cairo_image_surface_get_stride(blurred),
               pixels + (size_t)yy * w * 4, (size_t)w * 4);
    cairo_surface_mark_dirty(blurred);
    g_free(pixels); g_free(temp);
    cairo_save(cr);
    browser_rounded(cr, p->x, p->y, p->w, p->h, p->radius);
    cairo_clip(cr);
    cairo_set_source_surface(cr, blurred, ix, iy + offset);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(blurred);
}

static void browser_draw_content(GtkDrawingArea *area, cairo_t *cr,
                         int width, int height, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    const H2GPaint *p;
    PangoLayout *layout;
    PangoFontDescription *font;
    cairo_pattern_t *pattern;
    double x2, y2;
    double cx0, cy0, cx1, cy1;
    double offset;
    int i;
    (void)area; (void)width; (void)height;
    offset = gtk_adjustment_get_value(browser->adjustment);
    cairo_set_source_rgb(cr, .008, .024, .055);
    cairo_paint(cr);
    cairo_save(cr);
    cairo_translate(cr, 0, -offset);
    cairo_clip_extents(cr, &cx0, &cy0, &cx1, &cy1);
    layout = pango_cairo_create_layout(cr);
    font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    for (i = 0; i < h2g_canvas_count(browser->document); i++) {
        p = h2g_canvas_paint(browser->document, i);
        if (!p || p->w <= 0 || p->h <= 0) continue;
        if (p->y + p->h < cy0 || p->y > cy1 ||
            p->x + p->w < cx0 || p->x > cx1) continue;
        cairo_set_source_rgba(cr, p->fg.r, p->fg.g, p->fg.b, p->fg.a);
        if (p->type == 1) {
            browser_rounded(cr, p->x, p->y, p->w, p->h, p->radius);
            cairo_fill(cr);
        } else if (p->type == 3) {
            x2 = p->x + p->w;
            y2 = p->y;
            if (p->gradient_direction == 2) y2 += p->h;
            else if (p->gradient_direction == 3) { x2 = p->x; y2 += p->h; }
            pattern = cairo_pattern_create_linear(p->x, p->y, x2, y2);
            cairo_pattern_add_color_stop_rgba(pattern, 0,
                p->fg.r, p->fg.g, p->fg.b, p->fg.a);
            if (p->border_width)
                cairo_pattern_add_color_stop_rgba(pattern, .5,
                    p->via.r, p->via.g, p->via.b, p->via.a);
            cairo_pattern_add_color_stop_rgba(pattern, 1,
                p->end.r, p->end.g, p->end.b, p->end.a);
            browser_rounded(cr, p->x, p->y, p->w, p->h, p->radius);
            cairo_set_source(cr, pattern);
            cairo_fill(cr);
            cairo_pattern_destroy(pattern);
        } else if (p->type == 7) {
            browser_glow(cr, p);
        } else if (p->type == 6) {
            browser_frost(cr, p, offset);
        } else if (p->type == 5) {
            browser_shadow(cr, p);
        } else if (p->type == 4) {
            cairo_set_line_width(cr, p->border_width);
            browser_rounded(cr, p->x + p->border_width / 2.,
                            p->y + p->border_width / 2.,
                            p->w - p->border_width,
                            p->h - p->border_width, p->radius);
            cairo_stroke(cr);
        } else if (p->type == 2 && p->text) {
            pango_font_description_set_family(font, p->monospace ? "Monospace" : "Sans");
            pango_font_description_set_absolute_size(font,
                                                     p->font_size * PANGO_SCALE);
            pango_font_description_set_weight(font,
                                  (PangoWeight)(p->weight ? p->weight : 400));
            pango_layout_set_font_description(layout, font);
            if (p->markup) pango_layout_set_markup(layout, p->markup, -1);
            else pango_layout_set_text(layout, p->text, -1);
            pango_layout_set_width(layout, (int)(p->w * PANGO_SCALE));
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_line_spacing(layout,
                p->line_height > 0 ? p->line_height / 1.18 : 1.0);
            pango_layout_set_alignment(layout, p->align == 1 ? PANGO_ALIGN_CENTER :
                           (p->align == 2 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT));
            cairo_move_to(cr, p->x, p->y);
            pango_cairo_show_layout(cr, layout);
            if (p->gradient_start >= 0)
                browser_gradient_text(cr, layout, p);
        }
    }
    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_restore(cr);
}

/* Explicit viewport-sized image target makes backdrop sampling reliable
 * even when GTK hands the draw function a recording surface. */
static void browser_draw(GtkDrawingArea *area, cairo_t *cr,
                         int width, int height, gpointer user_data)
{
    cairo_surface_t *buffer;
    cairo_t *offscreen;
    if (width <= 0 || height <= 0) return;
    buffer = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(buffer) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(buffer);
        return;
    }
    offscreen = cairo_create(buffer);
    browser_draw_content(area, offscreen, width, height, user_data);
    cairo_destroy(offscreen);
    cairo_set_source_surface(cr, buffer, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(buffer);
}

static void browser_scrolled(GtkAdjustment *adjustment, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    (void)adjustment;
    gtk_widget_queue_draw(browser->surface);
}

static int browser_measure(const char *markup, int font_size, int weight,
                           int monospace, double width, double line_height,
                           void *user_data, int *natural_width)
{
    Browser *browser = (Browser *)user_data;
    PangoLayout *layout;
    PangoFontDescription *font;
    int h = 0, measured_width = 0;
    if (!browser->surface || !markup) return 0;
    layout = gtk_widget_create_pango_layout(browser->surface, NULL);
    font = pango_font_description_new();
    pango_font_description_set_family(font, monospace ? "Monospace" : "Sans");
    pango_font_description_set_absolute_size(font, font_size * PANGO_SCALE);
    pango_font_description_set_weight(font, (PangoWeight)weight);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_markup(layout, markup, -1);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_line_spacing(layout,
                  line_height > 0 ? line_height / 1.18 : 1.0);
    pango_layout_get_pixel_size(layout, &measured_width, &h);
    if (natural_width) *natural_width = measured_width;
    if (h < (int)(font_size * line_height + .5))
        h = (int)(font_size * line_height + .5);
    pango_font_description_free(font);
    g_object_unref(layout);
    return h;
}

static void browser_resize(GtkDrawingArea *area, int width,
                           int height, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    int document_height;
    double previous;
    (void)area;
    if (width <= 0 || height <= 0) return;
    if (browser->last_width == width && browser->last_height == height) return;
    browser->last_width = width;
    browser->last_height = height;
    previous = gtk_adjustment_get_value(browser->adjustment);
    h2g_canvas_set_text_measure(browser->document, browser_measure, browser);
    h2g_canvas_reflow(browser->document, width, height);
    document_height = h2g_canvas_document_height(browser->document);
    if (document_height < height) document_height = height;
    gtk_adjustment_configure(browser->adjustment, previous, 0,
                             document_height, 48, height * .90, height);
    gtk_widget_queue_draw(browser->surface);
    fprintf(stderr, "h2g-browser: viewport=%dx%d document_height=%d paints=%d\n",
            width, height, document_height, h2g_canvas_count(browser->document));
}

static void browser_press(GtkGestureClick *gesture, int count,
                          double x, double y, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    const char *href;
    H2GRect target;
    double max;
    (void)gesture; (void)count;
    y += gtk_adjustment_get_value(browser->adjustment);
    href = h2g_canvas_hit_href(browser->document, x, y);
    if (!href || href[0] != '#' || !href[1]) return;
    target = h2g_canvas_id_rect(browser->document, href + 1);
    if (!target.visible) return;
    max = gtk_adjustment_get_upper(browser->adjustment) -
          gtk_adjustment_get_page_size(browser->adjustment);
    gtk_adjustment_set_value(browser->adjustment,
                             target.y < max ? target.y : max);
}

static gboolean browser_wheel(GtkEventControllerScroll *controller,
                              double dx, double dy, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    double pos, high;
    (void)controller; (void)dx;
    pos = gtk_adjustment_get_value(browser->adjustment);
    high = gtk_adjustment_get_upper(browser->adjustment) -
           gtk_adjustment_get_page_size(browser->adjustment);
    pos += dy * 54;
    if (pos > high) pos = high;
    if (pos < 0) pos = 0;
    gtk_adjustment_set_value(browser->adjustment, pos);
    return TRUE;
}

static void browser_destroy(GtkWidget *widget, gpointer user_data)
{
    Browser *browser = (Browser *)user_data;
    (void)widget;
    g_main_loop_quit(browser->loop);
}

int main(int argc, char **argv)
{
    Browser browser;
    GtkWidget *box, *bar, *surface;
    GtkGesture *gesture;
    GtkEventController *scroll_controller;
    if (argc != 2) {
        fprintf(stderr, "usage: %s page.html\n", argv[0]);
        return 2;
    }
    memset(&browser, 0, sizeof(browser));
    browser.document = h2g_canvas_new(argv[1]);
    if (!gtk_init_check()) {
        fprintf(stderr, "GTK4 could not initialize a display\n");
        h2g_canvas_free(browser.document);
        return 1;
    }
    browser.window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(browser.window), "html2gtk — Canvas Prototype");
    gtk_window_set_default_size(GTK_WINDOW(browser.window), 1280, 850);
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(browser.window), box);
    surface = gtk_drawing_area_new();
    browser.surface = surface;
    gtk_widget_set_hexpand(surface, TRUE);
    gtk_widget_set_vexpand(surface, TRUE);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(surface), 500);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(surface), 500);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(surface), browser_draw,
                                   &browser, NULL);
    g_signal_connect(surface, "resize", G_CALLBACK(browser_resize), &browser);
    gtk_box_append(GTK_BOX(box), surface);

    browser.adjustment = gtk_adjustment_new(0, 0, 850, 48, 700, 850);
    g_signal_connect(browser.adjustment, "value-changed",
                     G_CALLBACK(browser_scrolled), &browser);
    bar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, browser.adjustment);
    gtk_box_append(GTK_BOX(box), bar);

    gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(browser_press), &browser);
    gtk_widget_add_controller(surface, GTK_EVENT_CONTROLLER(gesture));
    scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(browser_wheel), &browser);
    gtk_widget_add_controller(surface, scroll_controller);

    browser.loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(browser.window, "destroy", G_CALLBACK(browser_destroy), &browser);
    gtk_window_present(GTK_WINDOW(browser.window));
    g_main_loop_run(browser.loop);
    g_main_loop_unref(browser.loop);
    h2g_canvas_free(browser.document);
    return 0;
}
