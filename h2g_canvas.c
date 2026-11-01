/*
 * Experimental responsive HTML box tree (ISO C89). MIT.
 *
 * Bootstrap: reuse the validated HTML parser and Tailwind token resolver in
 * html2gtk.c without changing that stable GtkBuilder compiler. Both programs
 * share the same DOM implementation in this version. It will be factored into
 * a separate translation unit once the layout model settles.
 */
#define main html2gtk_legacy_entry
#include "html2gtk.c"
#undef main
#include "h2g_canvas.h"
#include "h2g_palette_v4.h"
#include <math.h>

#define H2G_PAINT_LIMIT 80000
#define H2G_TEXT_LIMIT 4096
#define H2G_MARKUP_LIMIT 32768
#define H2G_MAX_RECURSION 200

struct H2GCanvas {
    H2GPaint *paints;
    H2GRect *rects;
    int paint_count;
    int width, height, document_height;
    H2GTextMeasure measure;
    void *measure_data;
};

typedef struct H2GStyle {
    int hidden, display, inline_display, columns, first_column;
    int padding_l, padding_r, padding_t, padding_b;
    int margin_t, margin_b, gap;
    int font, weight, monospace, width, height, min_height, max_width, max_height;
    int gradient_direction;
    int has_from, has_via, has_to;
    H2GColor from, via, to;
    int centered, radius, border, text_clip, text_transparent, shadow_blur;
    int frost;
    int absolute, glow_blur, absolute_left, absolute_right, absolute_top, absolute_bottom;
    double leading, tracking;
    int leading_pixels;
    int flex_grow, justify, align_center, nowrap, text_align;
    H2GColor shadow_color;
    H2GColor color, background, border_color;
} H2GStyle;

static H2GColor rgba(double r, double g, double b, double a)
{
    H2GColor c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

static int css_color(const char *raw, H2GColor *out)
{
    char base[80], *end;
    const char *slash, *hex;
    size_t len;
    unsigned long rgb;
    long op = 100;
    slash = strrchr(raw, '/');
    len = slash ? (size_t)(slash - raw) : strlen(raw);
    if (len >= sizeof(base)) return 0;
    memcpy(base, raw, len); base[len] = 0;
    if (slash) {
        op = strtol(slash + 1, &end, 10);
        if (end == slash + 1 || *end || op < 0 || op > 100) return 0;
    }
    if (eq(base, "transparent")) {
        *out = rgba(0, 0, 0, 0); return 1;
    }
    {
        unsigned long i;
        for (i = 0; i < h2g_v4_color_count; i++) {
            if (eq(base, h2g_v4_colors[i].name)) {
                *out = rgba(h2g_v4_colors[i].r / 255.0,
                            h2g_v4_colors[i].g / 255.0,
                            h2g_v4_colors[i].b / 255.0,
                            (double)op / 100.0);
                return 1;
            }
        }
    }
    hex = tw_color(base);
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return 0;
    rgb = strtoul(hex + 1, &end, 16);
    if (*end) return 0;
    *out = rgba((double)((rgb >> 16) & 255) / 255.0,
                (double)((rgb >> 8) & 255) / 255.0,
                (double)(rgb & 255) / 255.0, (double)op / 100.0);
    return 1;
}

static int text_size(const char *v)
{
    static const char *names[] = {
        "xs", "sm", "base", "lg", "xl", "2xl", "3xl", "4xl", "5xl",
        "6xl", "7xl", "8xl", "9xl"
    };
    static const int sizes[] = {
        12, 14, 16, 18, 20, 24, 30, 36, 48, 60, 72, 96, 128
    };
    int i;
    if (*v == '[') return pixel_value(v);
    for (i = 0; i < 13; i++) if (eq(v, names[i])) return sizes[i];
    return -1;
}

static void style_defaults(H2GStyle *s, H2GStyle *parent)
{
    memset(s, 0, sizeof(*s));
    s->font = parent ? parent->font : 14;
    s->weight = parent ? parent->weight : 400;
    s->monospace = parent ? parent->monospace : 0;
    s->color = parent ? parent->color : rgba(.87, .91, .96, 1);
    s->background = rgba(0, 0, 0, 0);
    s->border_color = rgba(.2, .3, .4, 1);
    s->width = s->height = s->max_width = s->max_height = -1;
    s->columns = 1;
    s->shadow_color = rgba(0, 0, 0, .30);
    s->leading = parent ? parent->leading : 1.5;
    s->leading_pixels = parent ? parent->leading_pixels : 0;
    s->tracking = parent ? parent->tracking : 0.0;
    s->absolute_left = s->absolute_right = s->absolute_top = s->absolute_bottom = -100000;
}

static void style_apply(H2GStyle *s, const char *u)
{
    int n;
    H2GColor c;
    if (eq(u, "sr-only")) s->hidden = 1;
    else if (eq(u, "not-sr-only")) s->hidden = 0;
    else if (eq(u, "absolute")) s->absolute = 1;
    else if (eq(u, "relative")) s->absolute = 0;
    else if (eq(u, "blur-3xl")) s->glow_blur = 64;
    else if (eq(u, "blur-2xl")) s->glow_blur = 40;
    else if (eq(u, "blur-xl")) s->glow_blur = 24;
    else if (eq(u, "blur-lg")) s->glow_blur = 16;
    else if (begins(u, "-right-") && (n = pixel_value(u + 7)) >= 0) s->absolute_right = -n;
    else if (begins(u, "-top-") && (n = pixel_value(u + 5)) >= 0) s->absolute_top = -n;
    else if (begins(u, "-bottom-") && (n = pixel_value(u + 8)) >= 0) s->absolute_bottom = -n;
    else if (begins(u, "right-") && (n = pixel_value(u + 6)) >= 0) s->absolute_right = n;
    else if (begins(u, "left-") && (n = pixel_value(u + 5)) >= 0) s->absolute_left = n;
    else if (begins(u, "top-") && (n = pixel_value(u + 4)) >= 0) s->absolute_top = n;
    else if (begins(u, "bottom-") && (n = pixel_value(u + 7)) >= 0) s->absolute_bottom = n;
    else if (begins(u, "size-") && (n = pixel_value(u + 5)) >= 0) s->width = s->height = n;
    else if (eq(u, "leading-none")) { s->leading = 1.0; s->leading_pixels = 0; }
    else if (eq(u, "leading-tight")) { s->leading = 1.25; s->leading_pixels = 0; }
    else if (eq(u, "leading-snug")) { s->leading = 1.375; s->leading_pixels = 0; }
    else if (eq(u, "leading-normal")) { s->leading = 1.5; s->leading_pixels = 0; }
    else if (eq(u, "leading-relaxed")) { s->leading = 1.625; s->leading_pixels = 0; }
    else if (eq(u, "leading-loose")) { s->leading = 2.0; s->leading_pixels = 0; }
    else if (begins(u, "leading-[") && u[strlen(u)-1] == ']') {
        char *end;
        double v = strtod(u + 9, &end);
        if (*end == ']' && end[1] == 0 && v > 0.3 && v < 5) {
            s->leading = v;
            s->leading_pixels = 0;
        }
    }
    else if (begins(u, "leading-") && (n = pixel_value(u + 8)) >= 0)
        s->leading_pixels = n;
    else if (eq(u, "tracking-tighter")) s->tracking = -.05;
    else if (eq(u, "tracking-tight")) s->tracking = -.025;
    else if (eq(u, "tracking-normal")) s->tracking = 0;
    else if (eq(u, "tracking-wide")) s->tracking = .025;
    else if (eq(u, "tracking-wider")) s->tracking = .05;
    else if (eq(u, "tracking-widest")) s->tracking = .1;
    else if (begins(u, "tracking-[") && u[strlen(u)-1] == ']') {
        char *end;
        double v = strtod(u + 10, &end);
        if (strncmp(end, "em]", 3) == 0 && end[3] == 0 && v > -1 && v < 2) s->tracking = v;
    }
    else if (eq(u, "backdrop-blur")) s->frost = 8;
    else if (eq(u, "backdrop-blur-sm")) s->frost = 4;
    else if (eq(u, "backdrop-blur-md")) s->frost = 12;
    else if (eq(u, "backdrop-blur-lg")) s->frost = 16;
    else if (eq(u, "backdrop-blur-xl")) s->frost = 24;
    else if (eq(u, "backdrop-blur-2xl")) s->frost = 32;
    else if (eq(u, "backdrop-blur-none")) s->frost = 0;
    else if (eq(u, "bg-clip-text")) s->text_clip = 1;
    else if (eq(u, "text-transparent")) { s->text_transparent = 1; s->color.a = 0; }
    else if (eq(u, "shadow-none")) s->shadow_blur = 0;
    else if (eq(u, "shadow") || eq(u, "shadow-sm")) s->shadow_blur = 4;
    else if (eq(u, "shadow-md")) s->shadow_blur = 8;
    else if (eq(u, "shadow-lg")) s->shadow_blur = 14;
    else if (eq(u, "shadow-xl")) s->shadow_blur = 21;
    else if (eq(u, "shadow-2xl")) s->shadow_blur = 29;
    else if (begins(u, "shadow-") && css_color(u + 7, &c)) s->shadow_color = c;
    else if (eq(u, "flex-1") || eq(u, "grow")) s->flex_grow = 1;
    else if (eq(u, "justify-center")) s->justify = 1;
    else if (eq(u, "justify-between")) s->justify = 2;
    else if (eq(u, "justify-end")) s->justify = 3;
    else if (eq(u, "items-center")) s->align_center = 1;
    else if (eq(u, "whitespace-nowrap")) s->nowrap = 1;
    else if (eq(u, "text-center")) s->text_align = 1;
    else if (eq(u, "text-right")) s->text_align = 2;
    else if (eq(u, "hidden")) s->hidden = 1;
    else if (eq(u, "block") || eq(u, "inline") || eq(u, "inline-block")) {
        s->hidden = 0; s->display = 0;
        s->inline_display = !eq(u, "block");
    }
    else if (eq(u, "flex") || eq(u, "inline-flex") || eq(u, "flex-row")) {
        s->hidden = 0; s->display = 1;
        s->inline_display = eq(u, "inline-flex");
    }
    else if (eq(u, "flex-col")) { s->hidden = 0; s->display = 0; s->inline_display = 0; }
    else if (eq(u, "grid") || eq(u, "inline-grid")) {
        s->hidden = 0; s->display = 2;
        s->inline_display = eq(u, "inline-grid");
    }
    else if (begins(u, "bg-") && css_color(u + 3, &c)) s->background = c;
    else if (begins(u, "text-") && css_color(u + 5, &c)) s->color = c;
    else if (begins(u, "border-") && css_color(u + 7, &c)) s->border_color = c;
    else if (begins(u, "text-") && (n = text_size(u + 5)) >= 0) s->font = n;
    else if (eq(u, "font-black")) s->weight = 900;
    else if (eq(u, "font-extrabold")) s->weight = 800;
    else if (eq(u, "font-bold")) s->weight = 700;
    else if (eq(u, "font-semibold")) s->weight = 600;
    else if (eq(u, "font-medium")) s->weight = 500;
    else if (eq(u, "font-normal")) s->weight = 400;
    else if (eq(u, "font-light")) s->weight = 300;
    else if (eq(u, "font-thin")) s->weight = 100;
    else if (eq(u, "font-extralight")) s->weight = 200;
    else if (eq(u, "font-mono")) s->monospace = 1;
    else if (eq(u, "font-sans")) s->monospace = 0;
    else if (eq(u, "bg-linear-to-r")) s->gradient_direction = 1;
    else if (eq(u, "bg-linear-to-br")) s->gradient_direction = 2;
    else if (eq(u, "bg-linear-to-b")) s->gradient_direction = 3;
    else if (begins(u, "from-") && css_color(u + 5, &c)) { s->from = c; s->has_from = 1; }
    else if (begins(u, "via-") && css_color(u + 4, &c)) { s->via = c; s->has_via = 1; }
    else if (begins(u, "to-") && css_color(u + 3, &c)) { s->to = c; s->has_to = 1; }
    else if (begins(u, "p-") && (n = pixel_value(u + 2)) >= 0)
        s->padding_l = s->padding_r = s->padding_t = s->padding_b = n;
    else if (begins(u, "px-") && (n = pixel_value(u + 3)) >= 0)
        s->padding_l = s->padding_r = n;
    else if (begins(u, "py-") && (n = pixel_value(u + 3)) >= 0)
        s->padding_t = s->padding_b = n;
    else if (begins(u, "pt-") && (n = pixel_value(u + 3)) >= 0) s->padding_t = n;
    else if (begins(u, "pb-") && (n = pixel_value(u + 3)) >= 0) s->padding_b = n;
    else if (begins(u, "pl-") && (n = pixel_value(u + 3)) >= 0) s->padding_l = n;
    else if (begins(u, "pr-") && (n = pixel_value(u + 3)) >= 0) s->padding_r = n;
    else if (begins(u, "mt-") && (n = pixel_value(u + 3)) >= 0) s->margin_t = n;
    else if (begins(u, "mb-") && (n = pixel_value(u + 3)) >= 0) s->margin_b = n;
    else if (begins(u, "my-") && (n = pixel_value(u + 3)) >= 0) s->margin_t = s->margin_b = n;
    else if (begins(u, "gap-") && (n = pixel_value(u + 4)) >= 0) s->gap = n;
    else if (begins(u, "space-y-") && (n = pixel_value(u + 8)) >= 0) s->gap = n;
    else if (begins(u, "w-") && (n = pixel_value(u + 2)) >= 0) s->width = n;
    else if (begins(u, "h-") && (n = pixel_value(u + 2)) >= 0) s->height = n;
    else if (begins(u, "min-h-") && (n = pixel_value(u + 6)) >= 0) s->min_height = n;
    else if (begins(u, "max-w-") && (n = named_max_width(u + 6)) >= 0) s->max_width = n;
    else if (begins(u, "max-h-") && (n = pixel_value(u + 6)) >= 0) s->max_height = n;
    else if (eq(u, "mx-auto")) s->centered = 1;
    else if (eq(u, "rounded")) s->radius = 4;
    else if (eq(u, "rounded-sm")) s->radius = 4;
    else if (eq(u, "rounded-md")) s->radius = 6;
    else if (eq(u, "rounded-lg")) s->radius = 8;
    else if (eq(u, "rounded-xl")) s->radius = 12;
    else if (eq(u, "rounded-2xl")) s->radius = 16;
    else if (eq(u, "rounded-3xl")) s->radius = 24;
    else if (eq(u, "rounded-4xl")) s->radius = 32;
    else if (eq(u, "rounded-full")) s->radius = 999;
    else if (eq(u, "border")) s->border = 1;
    else if (eq(u, "border-2")) s->border = 2;
    else if (eq(u, "border-4")) s->border = 4;
}

static void style_compute(int n, H2GStyle *s, H2GStyle *parent)
{
    const char *p, *start, *state, *utility;
    char *word;
    int level, bp, cols, fixed;
    style_defaults(s, parent);
    if (eq(nodes[n].tag, "body")) s->background = rgba(.008, .024, .055, 1);
    if (eq(nodes[n].tag, "h1")) { s->font = 36; s->weight = 700; }
    if (eq(nodes[n].tag, "h2")) { s->font = 26; s->weight = 700; }
    if (eq(nodes[n].tag, "h3")) { s->font = 18; s->weight = 700; }
    if (eq(nodes[n].tag, "a")) s->color = rgba(.72, .82, .9, 1);
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (p == start) continue;
            word = dup_range(start, (size_t)(p - start));
            if (split_utility(word, &bp, &state, &utility) &&
                bp == breakpoints[level] && !*state)
                style_apply(s, utility);
            free(word);
        }
    }
    if (s->leading_pixels > 0 && s->font > 0)
        s->leading = (double)s->leading_pixels / s->font;
    grid_template(n, &cols, &fixed);
    s->columns = cols;
    s->first_column = fixed;
}

static int textual(int n)
{
    const char *tag = nodes[n].tag;
    return eq(tag, "#text") || eq(tag, "p") || eq(tag, "h1") ||
           eq(tag, "h2") || eq(tag, "h3") || eq(tag, "h4") ||
           eq(tag, "h5") || eq(tag, "h6") || eq(tag, "a") ||
           eq(tag, "button") || eq(tag, "label") || eq(tag, "small") ||
           eq(tag, "code") || eq(tag, "pre") || eq(tag, "blockquote") ||
           eq(tag, "summary") || eq(tag, "li") || eq(tag, "dt") ||
           eq(tag, "dd") || eq(tag, "strong") || eq(tag, "em") ||
           eq(tag, "span") || eq(tag, "time") || eq(tag, "th") ||
           eq(tag, "td") || eq(tag, "option");
}

static void flatten_lite(int n, char *buf, size_t *used, int depth)
{
    int c;
    const char *p;
    size_t len;
    if (depth > 80) return;
    if (eq(nodes[n].tag, "br")) p = "\n";
    else if (eq(nodes[n].tag, "#text")) p = nodes[n].text;
    else p = NULL;
    if (p) {
        len = strlen(p);
        if (*used + len >= H2G_TEXT_LIMIT) len = H2G_TEXT_LIMIT - 1 - *used;
        memcpy(buf + *used, p, len); *used += len; buf[*used] = 0;
    } else for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
        if (*used >= H2G_TEXT_LIMIT - 1) break;
        flatten_lite(c, buf, used, depth + 1);
    }
}

static H2GPaint *new_paint(H2GCanvas *c, int type, int node,
                           double x, double y, double w, double h)
{
    H2GPaint *p;
    if (c->paint_count >= H2G_PAINT_LIMIT) return NULL;
    p = &c->paints[c->paint_count++];
    memset(p, 0, sizeof(*p));
    p->type = type; p->node = node;
    p->x = x; p->y = y; p->w = w; p->h = h;
    return p;
}

/* Build Pango markup without carrying HTML markup into the renderer.
 * The original text bytes remain separately available for hit testing. */
static void markup_append(char *buf, size_t *len, const char *v)
{
    size_t n = strlen(v);
    if (*len + n >= H2G_MARKUP_LIMIT) n = H2G_MARKUP_LIMIT - 1 - *len;
    memcpy(buf + *len, v, n);
    *len += n;
    buf[*len] = 0;
}

static void markup_escape(char *buf, size_t *len, const char *v)
{
    while (*v && *len < H2G_MARKUP_LIMIT - 8) {
        if (*v == '&') markup_append(buf, len, "&amp;");
        else if (*v == '<') markup_append(buf, len, "&lt;");
        else if (*v == '>') markup_append(buf, len, "&gt;");
        else if (*v == '\"') markup_append(buf, len, "&quot;");
        else {
            buf[(*len)++] = *v;
            buf[*len] = 0;
        }
        v++;
    }
}

static void markup_subtree(int n, const H2GStyle *inherited,
                           char *markup, size_t *mlen, int *plain,
                           H2GPaint *paint, int depth)
{
    H2GStyle st;
    int c, open = 0;
    char tag[256], rgb[16];
    const char *v;
    if (depth > 64 || *mlen >= H2G_MARKUP_LIMIT - 256) return;
    if (eq(nodes[n].tag, "#text")) {
        v = nodes[n].text ? nodes[n].text : "";
        markup_escape(markup, mlen, v);
        *plain += (int)strlen(v);
        return;
    }
    if (eq(nodes[n].tag, "br")) {
        markup_append(markup, mlen, "\n"); (*plain)++; return;
    }
    style_compute(n, &st, (H2GStyle *)inherited);
    if (depth > 0) {
        if (st.text_clip && st.gradient_direction && st.has_from) {
            if (paint->gradient_start < 0) paint->gradient_start = *plain;
            paint->text_from = st.from;
            paint->text_via = st.has_via ? st.via : st.from;
            paint->text_to = st.has_to ? st.to : st.from;
            paint->text_has_via = st.has_via;
        }
        {
            H2GColor ink = (st.text_clip && st.has_from) ? st.from : st.color;
            sprintf(rgb, "#%02x%02x%02x", (int)(ink.r * 255 + .5),
                    (int)(ink.g * 255 + .5), (int)(ink.b * 255 + .5));
        }
        sprintf(tag, "<span foreground=\"%s\" weight=\"%d\"", rgb, st.weight);
        markup_append(markup, mlen, tag);
        if (st.text_clip && st.has_from)
            markup_append(markup, mlen, " alpha=\"0%\"");
        if (st.monospace) markup_append(markup, mlen, " font_family=\"monospace\"");
        if (fabs(st.tracking - inherited->tracking) > .0001) {
            sprintf(tag, " letter_spacing=\"%d\"", (int)(st.tracking * st.font * 768));
            markup_append(markup, mlen, tag);
        }
        if (st.font != inherited->font) {
            sprintf(tag, " size=\"%d\"", st.font * 768);
            markup_append(markup, mlen, tag);
        }
        if (eq(nodes[n].tag, "em") || eq(nodes[n].tag, "i"))
            markup_append(markup, mlen, " style=\"italic\"");
        if (eq(nodes[n].tag, "u") || eq(nodes[n].tag, "ins"))
            markup_append(markup, mlen, " underline=\"single\"");
        if (eq(nodes[n].tag, "s") || eq(nodes[n].tag, "del"))
            markup_append(markup, mlen, " strikethrough=\"true\"");
        markup_append(markup, mlen, ">"); open = 1;
    }
    {
        int previous = -1;
        for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
            if (previous >= 0 && st.display == 1 && st.gap > 0 &&
                (eq(nodes[n].tag, "a") || eq(nodes[n].tag, "button"))) {
                markup_append(markup, mlen, "  ");
                *plain += 2;
            }
            markup_subtree(c, &st, markup, mlen, plain, paint, depth + 1);
            previous = c;
        }
    }
    if (open) {
        markup_append(markup, mlen, "</span>");
        if (st.text_clip && paint->gradient_start >= 0 && paint->gradient_end < 0)
            paint->gradient_end = *plain;
    }
}

static int visible_child(int n)
{
    /* Do not layout CSS, metadata, SVG subtrees or invisible tags. */
    return !eq(nodes[n].tag, "head") && !eq(nodes[n].tag, "style") &&
           !eq(nodes[n].tag, "script") && !eq(nodes[n].tag, "meta") &&
           !eq(nodes[n].tag, "link") && !eq(nodes[n].tag, "title") &&
           !eq(nodes[n].tag, "svg") && !eq(nodes[n].tag, "defs") &&
           !eq(nodes[n].tag, "option") && !class_active(n, "absolute");
}

static int text_height(const char *text, int font, double width)
{
    const unsigned char *p = (const unsigned char *)text;
    int columns, used = 0, lines = 1;
    double cw = font * 0.54;
    columns = (int)(width / (cw > 0 ? cw : 8));
    if (columns < 1) columns = 1;
    while (*p) {
        if (*p == '\n') { lines++; used = 0; }
        else if ((*p & 0xc0) != 0x80) {
            used++;
            if (used > columns) { lines++; used = 1; }
        }
        p++;
    }
    return lines * (int)(font * 1.47 + 0.5);
}

static double layout_node(H2GCanvas *c, int n, double x, double y,
                          double available, const H2GStyle *inherited, int depth)
{
    H2GStyle st, child;
    H2GPaint *paint;
    H2GRect *r;
    int a, count_children, col, row, i, ncols, *row_first;
    double width, inner_width, left, top, total, h, child_h;
    double cell_width, cell_x, max_row_h, gap, *heights;
    char textbuf[H2G_TEXT_LIMIT];
    size_t used = 0;
    int is_text;
    if (depth >= H2G_MAX_RECURSION || !visible_child(n)) return 0;
    style_compute(n, &st, (H2GStyle *)inherited);
    if (eq(nodes[n].tag, "button")) {
        if (!st.padding_t && !st.padding_b) {
            st.padding_t = 8; st.padding_b = 8;
        }
        if (!st.padding_l && !st.padding_r) {
            st.padding_l = 12; st.padding_r = 12;
        }
    }
    r = &c->rects[n];
    if (st.hidden) return 0;
    width = available;
    if (st.width >= 0 && st.width < width) width = st.width;
    if (st.max_width >= 0 && st.max_width < width) width = st.max_width;
    /* Unlike display:flex, inline-flex is intrinsic-width. A badge should
     * not consume the full page just because the parent is wide. */
    if (st.inline_display && st.display == 1) {
        double needed = st.padding_l + st.padding_r;
        int seen = 0;
        for (a = nodes[n].first; a >= 0; a = nodes[a].next) {
            H2GStyle part;
            size_t len = 0;
            double glyph_width = 0;
            if (!visible_child(a) || resolved_hidden(a)) continue;
            style_compute(a, &part, &st);
            if (part.width >= 0) glyph_width = part.width;
            else if (textual(a)) {
                textbuf[0] = 0;
                flatten_lite(a, textbuf, &len, 0);
                glyph_width = len * part.font * (part.monospace ? .60 : .55);
                if (c->measure && len) {
                    char escaped[H2G_MARKUP_LIMIT];
                    size_t used_escaped = 0;
                    int natural = 0;
                    escaped[0] = 0;
                    markup_escape(escaped, &used_escaped, textbuf);
                    c->measure(escaped, part.font, part.weight, part.monospace,
                               100000, part.leading, c->measure_data, &natural);
                    if (natural > 0) glyph_width = natural;
                }
                glyph_width += part.padding_l + part.padding_r;
            }
            if (seen) needed += st.gap;
            needed += glyph_width;
            seen++;
        }
        if (needed < width && needed > 0) width = needed;
    }
    if (width < 1) width = 1;
    if (st.centered && width < available) x += (available - width) / 2;
    inner_width = width - st.padding_l - st.padding_r;
    if (inner_width < 1) inner_width = 1;
    left = x + st.padding_l;
    top = y + st.margin_t + st.padding_t;
    r->visible = 1; r->x = x; r->y = y + st.margin_t; r->w = width;
    if (st.frost > 0) {
        paint = new_paint(c, 6, n, x, y + st.margin_t, width, 1);
        if (paint) {
            paint->radius = st.radius;
            paint->shadow_blur = st.frost;
        }
    }
    if (st.shadow_blur > 0) {
        paint = new_paint(c, 5, n, x, y + st.margin_t, width, 1);
        if (paint) {
            paint->fg = st.shadow_color;
            paint->radius = st.radius;
            paint->shadow_blur = st.shadow_blur;
        }
    }
    if (st.gradient_direction && (st.has_from || st.has_to)) {
        paint = new_paint(c, 3, n, x, y + st.margin_t, width, 1);
        if (paint) {
            paint->fg = st.has_from ? st.from : st.background;
            paint->via = st.has_via ? st.via : st.background;
            paint->end = st.has_to ? st.to : st.background;
            paint->gradient_direction = st.gradient_direction;
            paint->border_width = st.has_via;
            paint->radius = st.radius;
        }
    } else if (st.background.a > 0) {
        paint = new_paint(c, 1, n, x, y + st.margin_t, width, 1);
        if (paint) { paint->fg = st.background; paint->radius = st.radius; }
    }
    /* Decorative absolute blurred blobs are paint commands, not layout
     * children. Their positions are relative to the containing box. */
    if (!textual(n)) {
        for (a = nodes[n].first; a >= 0; a = nodes[a].next) {
            H2GStyle glow;
            if (!class_active(a, "absolute")) continue;
            style_compute(a, &glow, &st);
            if (glow.glow_blur <= 0 || glow.background.a <= 0 ||
                glow.width <= 0 || glow.height <= 0) continue;
            paint = new_paint(c, 7, n, x, y + st.margin_t,
                              glow.width, glow.height);
            if (!paint) continue;
            paint->fg = glow.background;
            paint->shadow_blur = glow.glow_blur;
            paint->clip_x = x;
            paint->clip_y = y + st.margin_t;
            paint->clip_w = width;
            paint->radius = st.radius;
            paint->gradient_direction = 0;
            if (glow.absolute_left != -100000)
                paint->x = x + glow.absolute_left;
            else if (glow.absolute_right != -100000)
                paint->x = x + width - glow.width - glow.absolute_right;
            if (glow.absolute_top != -100000)
                paint->y = y + st.margin_t + glow.absolute_top;
            else if (glow.absolute_bottom != -100000) {
                paint->y = (double)glow.absolute_bottom;
                paint->gradient_direction = 99;
            }
        }
    }
    if (st.border > 0) {
        paint = new_paint(c, 4, n, x, y + st.margin_t, width, 1);
        if (paint) {
            paint->fg = st.border_color;
            paint->border_width = st.border;
            paint->radius = st.radius;
        }
    }
    is_text = textual(n);
    if (is_text) {
        textbuf[0] = 0;
        flatten_lite(n, textbuf, &used, 0);
        h = text_height(textbuf, st.font, inner_width);
        if (st.leading > 0) h = (int)(h * st.leading / 1.47 + .5);
        paint = new_paint(c, 2, n, left, top, inner_width, h);
        if (paint) {
            char buf[H2G_MARKUP_LIMIT];
            size_t mlen = 0;
            int plain_bytes = 0;
            buf[0] = 0;
            paint->gradient_start = paint->gradient_end = -1;
            markup_subtree(n, &st, buf, &mlen, &plain_bytes, paint, 0);
            if (fabs(st.tracking) > .0001 && mlen + 64 < H2G_MARKUP_LIMIT) {
                char prefix[80];
                size_t prefix_size;
                sprintf(prefix, "<span letter_spacing=\"%d\">",
                        (int)(st.tracking * st.font * 768));
                prefix_size = strlen(prefix);
                memmove(buf + prefix_size, buf, mlen + 1);
                memcpy(buf, prefix, prefix_size);
                mlen += prefix_size;
                markup_append(buf, &mlen, "</span>");
            }
            paint->markup = dup_s(buf);
            paint->fg = st.color; paint->font_size = st.font;
            paint->bold = st.weight >= 700;
            paint->weight = st.weight;
            paint->monospace = st.monospace;
            paint->align = st.text_align;
            paint->line_height = st.leading;
            paint->tracking = st.tracking;
            paint->text = dup_s(textbuf);
            if (c->measure) {
                int measured = c->measure(paint->markup, st.font, st.weight,
                                  st.monospace, inner_width, st.leading,
                                  c->measure_data, NULL);
                if (measured > 0) { h = measured; paint->h = measured; }
            }
        }
        total = h + st.padding_t + st.padding_b;
    } else if (st.display == 2 && st.columns > 1) {
        ncols = st.columns;
        if (ncols > 12) ncols = 12;
        gap = (double)st.gap;
        cell_width = (inner_width - (ncols - 1) * gap) / ncols;
        if (cell_width < 1) cell_width = 1;
        row_first = (int *)xalloc((size_t)count * sizeof(int));
        heights = (double *)xalloc((size_t)ncols * sizeof(double));
        for (a = 0; a < ncols; a++) heights[a] = 0;
        row = 0; col = 0; max_row_h = 0; count_children = 0;
        for (a = nodes[n].first; a >= 0; a = nodes[a].next) {
            if (!visible_child(a)) continue;
            if (resolved_hidden(a)) continue;
            row_first[count_children++] = a;
        }
        for (i = 0; i < count_children; i++) {
            a = row_first[i];
            if (col >= ncols) { top += max_row_h + gap; max_row_h = 0; col = 0; row++; }
            if (st.first_column > 0 && ncols == 2) {
                cell_x = (col == 0) ? left : left + st.first_column + gap;
                cell_width = (col == 0) ? st.first_column : inner_width - st.first_column - gap;
                if (cell_width < 1) cell_width = 1;
            } else {
                cell_width = (inner_width - (ncols - 1) * gap) / ncols;
                cell_x = left + col * (cell_width + gap);
            }
            child_h = layout_node(c, a, cell_x, top, cell_width, &st, depth + 1);
            if (child_h > max_row_h) max_row_h = child_h;
            col++;
        }
        total = (top - (y + st.margin_t + st.padding_t)) + max_row_h + st.padding_t + st.padding_b;
        free(heights); free(row_first);
        (void)row;
    } else if (st.display == 1) {
        /* Natural-size flex row; free space belongs to grow items or
         * justify-* rather than being divided between every child. */
        int *items;
        double *sizes;
        int growers, k;
        double wanted, remaining, cursor, extra_gap, available_space;
        H2GStyle cs;
        count_children = 0;
        for (a = nodes[n].first; a >= 0; a = nodes[a].next)
            if (visible_child(a) && !resolved_hidden(a)) count_children++;
        items = (int *)xalloc((size_t)(count_children + 1) * sizeof(int));
        sizes = (double *)xalloc((size_t)(count_children + 1) * sizeof(double));
        wanted = 0; growers = 0; k = 0;
        for (a = nodes[n].first; a >= 0; a = nodes[a].next) {
            size_t length = 0;
            if (!visible_child(a) || resolved_hidden(a)) continue;
            items[k] = a;
            style_compute(a, &cs, &st);
            if (cs.flex_grow) {
                sizes[k] = 0;
                growers++;
            } else if (cs.width >= 0) {
                sizes[k] = cs.width;
            } else if (textual(a)) {
                double glyph_w;
                textbuf[0] = 0;
                flatten_lite(a, textbuf, &length, 0);
                glyph_w = (double)length * cs.font *
                          (cs.monospace ? .60 : .55);
                if (c->measure && length < H2G_TEXT_LIMIT - 1) {
                    char escaped[H2G_MARKUP_LIMIT];
                    size_t escaped_len = 0;
                    int natural = 0;
                    escaped[0] = 0;
                    markup_escape(escaped, &escaped_len, textbuf);
                    c->measure(escaped, cs.font, cs.weight, cs.monospace,
                               100000, cs.leading, c->measure_data, &natural);
                    if (natural > 0) glyph_w = (double)natural;
                }
                sizes[k] = glyph_w + cs.padding_l + cs.padding_r;
                if (eq(nodes[a].tag, "button") &&
                    !cs.padding_l && !cs.padding_r) sizes[k] += 24;
                if (sizes[k] < 20) sizes[k] = 20;
            } else {
                sizes[k] = 0;
                growers++;
            }
            wanted += sizes[k];
            k++;
        }
        available_space = inner_width - (count_children > 0 ?
                        (count_children - 1) * st.gap : 0);
        if (available_space < 1) available_space = 1;
        remaining = available_space - wanted;
        if (remaining < 0 && wanted > 0) {
            double scale = available_space / wanted;
            for (k = 0; k < count_children; k++)
                if (sizes[k] > 0) sizes[k] *= scale;
            remaining = 0;
        }
        if (growers > 0) {
            double share = remaining / growers;
            for (k = 0; k < count_children; k++) {
                style_compute(items[k], &cs, &st);
                if (cs.flex_grow || (!textual(items[k]) && cs.width < 0))
                    sizes[k] = share;
            }
            remaining = 0;
        }
        extra_gap = 0;
        cursor = left;
        if (st.justify == 1) cursor += remaining / 2;
        if (st.justify == 3) cursor += remaining;
        if (st.justify == 2 && count_children > 1)
            extra_gap = remaining / (count_children - 1);
        max_row_h = 0;
        for (k = 0; k < count_children; k++) {
            double item_w = sizes[k];
            if (item_w < 1) item_w = 1;
            child_h = layout_node(c, items[k], cursor, top, item_w,
                                  &st, depth + 1);
            if (child_h > max_row_h) max_row_h = child_h;
            cursor += item_w + st.gap + extra_gap;
        }
        free(sizes); free(items);
        total = max_row_h + st.padding_t + st.padding_b;
    } else {
        h = 0; count_children = 0;
        for (a = nodes[n].first; a >= 0; a = nodes[a].next) {
            if (!visible_child(a) || resolved_hidden(a)) continue;
            if (count_children) h += st.gap;
            child_h = layout_node(c, a, left, top + h, inner_width, &st, depth + 1);
            h += child_h;
            count_children++;
        }
        total = h + st.padding_t + st.padding_b;
    }
    if (st.height >= 0 && total < st.height) total = st.height;
    if (st.min_height > total) total = st.min_height;
    if (total < 1 && (st.background.a > 0 || st.gradient_direction)) total = 1;
    r->h = total;
    /* Paint geometry is known only after child layout. */
    for (i = c->paint_count - 1; i >= 0; i--) {
        if (c->paints[i].node == n &&
            (c->paints[i].type == 1 || c->paints[i].type == 3 ||
             c->paints[i].type == 4 || c->paints[i].type == 5 ||
             c->paints[i].type == 6))
            c->paints[i].h = total;
    }
    for (i = c->paint_count - 1; i >= 0; i--) {
        if (c->paints[i].node == n && c->paints[i].type == 7) {
            c->paints[i].clip_h = total;
            if (c->paints[i].gradient_direction == 99) {
                c->paints[i].y = y + st.margin_t + total -
                    c->paints[i].h - c->paints[i].y;
                c->paints[i].gradient_direction = 0;
            }
        }
    }
    child = st; (void)child;
    return total + st.margin_t + st.margin_b;
}

H2GCanvas *h2g_canvas_new(const char *filename)
{
    H2GCanvas *canvas;
    char *src;
    count = warnings = 0;
    source_name = filename;
    src = read_file(filename);
    parse_html(src);
    free(src);
    canvas = (H2GCanvas *)xalloc(sizeof(*canvas));
    memset(canvas, 0, sizeof(*canvas));
    canvas->paints = (H2GPaint *)xalloc(H2G_PAINT_LIMIT * sizeof(H2GPaint));
    canvas->rects = (H2GRect *)xalloc(MAX_NODES * sizeof(H2GRect));
    return canvas;
}

void h2g_canvas_reflow(H2GCanvas *canvas, int width, int height)
{
    int i, n;
    double total = 0, h;
    H2GStyle base;
    if (width < 240) width = 240;
    if (height < 100) height = 100;
    for (i = 0; i < canvas->paint_count; i++) {
        free(canvas->paints[i].text);
        free(canvas->paints[i].markup);
    }
    canvas->paint_count = 0;
    memset(canvas->rects, 0, MAX_NODES * sizeof(H2GRect));
    canvas->width = viewport_width = width;
    canvas->height = height;
    style_defaults(&base, NULL);
    for (n = nodes[0].first; n >= 0; n = nodes[n].next) {
        h = layout_node(canvas, n, 0, total, width, &base, 0);
        total += h;
    }
    canvas->document_height = (int)(total + 0.5);
}

void h2g_canvas_set_text_measure(H2GCanvas *canvas, H2GTextMeasure measure,
                                 void *user_data)
{
    if (!canvas) return;
    canvas->measure = measure;
    canvas->measure_data = user_data;
}

int h2g_canvas_count(const H2GCanvas *canvas) { return canvas->paint_count; }
const H2GPaint *h2g_canvas_paint(const H2GCanvas *canvas, int index)
{
    if (index < 0 || index >= canvas->paint_count) return NULL;
    return &canvas->paints[index];
}
H2GRect h2g_canvas_id_rect(const H2GCanvas *canvas, const char *id)
{
    H2GRect empty;
    int n;
    memset(&empty, 0, sizeof(empty));
    for (n = 1; n < count; n++)
        if (eq(attr(n, "id"), id) && *id) return canvas->rects[n];
    return empty;
}
int h2g_canvas_node_count(const H2GCanvas *canvas) { (void)canvas; return count; }
int h2g_canvas_document_height(const H2GCanvas *canvas) { return canvas->document_height; }
int h2g_canvas_columns(const H2GCanvas *canvas, const char *id)
{
    int n, cols, fixed;
    (void)canvas;
    for (n = 1; n < count; n++) if (*id && eq(attr(n, "id"), id)) {
        grid_template(n, &cols, &fixed);
        return cols;
    }
    return -1;
}
const char *h2g_canvas_hit_href(const H2GCanvas *canvas, double x, double y)
{
    int n, winner = -1;
    H2GRect r;
    for (n = 1; n < count; n++) {
        if (nodes[n].tag && eq(nodes[n].tag, "a") && *attr(n, "href")) {
            r = canvas->rects[n];
            if (r.visible && x >= r.x && y >= r.y &&
                x < r.x + r.w && y < r.y + r.h) winner = n;
        }
    }
    return winner >= 0 ? attr(winner, "href") : NULL;
}
void h2g_canvas_free(H2GCanvas *canvas)
{
    int n;
    if (!canvas) return;
    for (n = 0; n < canvas->paint_count; n++) {
        free(canvas->paints[n].text);
        free(canvas->paints[n].markup);
    }
    free(canvas->paints); free(canvas->rects); free(canvas);
    for (n = 0; n < count; n++) {
        int a;
        free(nodes[n].tag); free(nodes[n].text);
        for (a = 0; a < nodes[n].attr_count; a++) {
            free(nodes[n].attrs[a].name); free(nodes[n].attrs[a].value);
        }
    }
    count = 0;
}
