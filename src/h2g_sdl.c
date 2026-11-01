/*
 * html2gui SDL3 proof-of-renderer.
 * Strict ISO C89 source. Lexbor is isolated behind parser.h.
 *
 * This is deliberately a limited layout/paint implementation, NOT a CSS
 * conforming layout engine. It demonstrates the full pipeline:
 * Lexbor DOM -> Lexbor CSS selector match -> computed subset -> box layout
 * -> cached SDL textures -> SDL3 window and events.
 */
#include "html2gui/parser.h"
#include "html2gui/renderer.h"
#include "h2g_text_input.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define H2G_MAX_OPS 60000
#define H2G_MAX_FONTS 32
#define H2G_MAX_SHEETS 16
#define H2G_MAX_DEPTH 128
#define H2G_MAX_TEXT 32768
#define H2G_MAX_VARS 2048
#define H2G_LINE 4096
#define H2G_PROP_COUNT 19
#define H2G_MAX_INPUTS 256

typedef struct H2GColor { unsigned char r, g, b, a; } H2GColor;
typedef struct H2GStyle {
    H2GColor fg, bg;
    int size, bold, display, flex_column, position, clipped, grid_columns;
    float margin, padding, gap, width, height, max_width, grid_fixed_first;
    int specified[H2G_PROP_COUNT];
    int imp[H2G_PROP_COUNT];
    unsigned long spec[H2G_PROP_COUNT], order[H2G_PROP_COUNT];
} H2GStyle;
typedef struct H2GVariable {
    char *name;
    char *value;
} H2GVariable;

typedef struct H2GInput {
    const H2GDomNode *node;
    char id[96];
    H2GTextInput editor;
    size_t view_start, view_end;
    float text_x;
    int font_size, font_bold;
} H2GInput;

typedef struct H2GFont {
    int size, bold;
    TTF_Font *font;
} H2GFont;
typedef struct H2GOp {
    int type;
    SDL_FRect rect;
    H2GColor color;
    SDL_Texture *texture;
    char id[96];
    H2GInput *input;
} H2GOp;
typedef struct H2GApp {
    H2GHtmlDocument *html;
    H2GCssStylesheet *sheets[H2G_MAX_SHEETS];
    int sheet_count;
    SDL_Window *window;
    SDL_Renderer *renderer;
    H2GFont fonts[H2G_MAX_FONTS];
    int font_count;
    const char *font_path;
    H2GVariable *vars;
    int var_count;
    H2GOp *ops;
    int op_count, overflow, viewport_w, viewport_h;
    int dark_backgrounds;
    float document_h, scroll, first_h1_y;
    H2GButtonCallback on_button;
    H2GInputCallback on_input;
    void *callback_user;
    H2GInput *inputs;
    H2GInput *focused_input;
    int input_count;
} H2GApp;
typedef struct H2GMatchApply {
    H2GStyle *style;
    H2GApp *app;
    unsigned long base;
} H2GMatchApply;

static H2GColor color(unsigned char r, unsigned char g,
                      unsigned char b, unsigned char a)
{
    H2GColor c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

static char *read_file(const char *filename, size_t *length)
{
    FILE *f;
    long n;
    char *buffer;
    size_t got;
    f = fopen(filename, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if (n < 0 || n > 33554432L || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f); return NULL;
    }
    buffer = (char *)malloc((size_t)n + 1);
    if (!buffer) { fclose(f); return NULL; }
    got = fread(buffer, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buffer); return NULL; }
    buffer[got] = '\0';
    *length = got;
    return buffer;
}

static int tag_is(const H2GDomNode *node, const char *name)
{
    size_t n;
    const char *tag = h2g_dom_element_name(node, &n);
    return tag != NULL && strlen(name) == n && memcmp(tag, name, n) == 0;
}

static int css_value(const char *s, float *value)
{
    char *end;
    double d = strtod(s, &end);
    if (end == s) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end == '\0' || strcmp(end, "px") == 0) {
        *value = (float)d; return 1;
    }
    if (strcmp(end, "rem") == 0 || strcmp(end, "em") == 0) {
        *value = (float)(d * 16.0); return 1;
    }
    return 0;
}


static char *h2g_string_copy(const char *s)
{
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, s, length + 1);
    return copy;
}

static const char *find_var(const H2GApp *app, const char *name)
{
    int i;
    for (i = app->var_count - 1; i >= 0; i--)
        if (strcmp(app->vars[i].name, name) == 0)
            return app->vars[i].value;
    return NULL;
}

static void capture_var(const char *name, const char *value,
    unsigned long specificity, int important, unsigned long order, void *ctx)
{
    H2GApp *app = (H2GApp *)ctx;
    H2GVariable *item;
    int i;
    char *copied;
    (void)specificity; (void)important; (void)order;
    if (name[0] != '-' || name[1] != '-' || app->vars == NULL)
        return;
    copied = h2g_string_copy(value);
    if (copied == NULL) return;
    for (i = 0; i < app->var_count; i++)
        if (strcmp(app->vars[i].name, name) == 0) {
            free(app->vars[i].value);
            app->vars[i].value = copied;
            return;
        }
    if (app->var_count == H2G_MAX_VARS) { free(copied); return; }
    item = &app->vars[app->var_count];
    item->name = h2g_string_copy(name);
    if (item->name == NULL) { free(copied); return; }
    item->value = copied;
    app->var_count++;
}

/* Small CSS value evaluator for the first SDL slice.  This is not a
 * substitute for CSS Values & Units, calculation trees, or inheritance. */
static const char *resolve_value(const H2GApp *app, const char *value,
                                 char *out, size_t capacity)
{
    const char *start, *end, *substitution, *mul;
    char name[256];
    size_t n;
    float numeric;
    double multiplier;
    if (strncmp(value, "var(", 4) == 0) {
        start = value + 4;
        end = strchr(start, ')');
        if (end != NULL) {
            n = (size_t)(end - start);
            if (n != 0 && n < sizeof(name)) {
                memcpy(name, start, n); name[n] = '\0';
                substitution = find_var(app, name);
                if (substitution != NULL) {
                    if (strlen(substitution) >= capacity) return value;
                    strcpy(out, substitution);
                    return out;
                }
            }
        }
    }
    if (strncmp(value, "calc(var(", 9) == 0) {
        start = value + 9;
        end = strchr(start, ')');
        if (end == NULL) return value;
        n = (size_t)(end - start);
        if (n == 0 || n >= sizeof(name)) return value;
        memcpy(name, start, n); name[n] = '\0';
        substitution = find_var(app, name);
        if (substitution == NULL || !css_value(substitution, &numeric))
            return value;
        mul = strchr(end + 1, '*');
        if (mul == NULL) return value;
        multiplier = strtod(mul + 1, NULL);
        if (capacity < 48) return value;
        sprintf(out, "%.5gpx", numeric * multiplier);
        return out;
    }
    return value;
}

static double h2g_clip_channel(double value)
{
    if (value <= 0) return 0;
    if (value >= 1) return 1;
    return value;
}

static unsigned char h2g_srgb_byte(double linear)
{
    double v;
    linear = h2g_clip_channel(linear);
    if (linear <= 0.0031308) v = 12.92 * linear;
    else v = 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
    v = h2g_clip_channel(v);
    return (unsigned char)(v * 255.0 + .5);
}

/* OKLCH -> Oklab -> linear sRGB -> encoded sRGB. */
static int h2g_oklch(const char *value, H2GColor *out)
{
    double l, c, hue, a, b, ll, mm, ss;
    double lr, lg, lb;
    const double pi = 3.14159265358979323846;
    if (sscanf(value, "oklch( %lf%% %lf %lf", &l, &c, &hue) != 3)
        return 0;
    l /= 100.0;
    hue *= pi / 180.0;
    a = c * cos(hue);
    b = c * sin(hue);
    ll = l + 0.3963377774 * a + 0.2158037573 * b;
    mm = l - 0.1055613458 * a - 0.0638541728 * b;
    ss = l - 0.0894841775 * a - 1.2914855480 * b;
    ll *= ll * ll;
    mm *= mm * mm;
    ss *= ss * ss;
    lr = 4.0767416621 * ll - 3.3077115913 * mm + 0.2309699292 * ss;
    lg = -1.2684380046 * ll + 2.6097574011 * mm - 0.3413193965 * ss;
    lb = -0.0041960863 * ll - 0.7034186147 * mm + 1.7076147010 * ss;
    *out = color(h2g_srgb_byte(lr), h2g_srgb_byte(lg),
                 h2g_srgb_byte(lb), 255);
    return 1;
}

static int parse_color(const char *v, H2GColor *out)
{
    unsigned int r, g, b, a = 255;
    size_t n = strlen(v);
    if (strncmp(v, "oklch(", 6) == 0)
        return h2g_oklch(v, out);
    if (n == 7 && v[0] == '#' &&
        sscanf(v + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
        *out = color((unsigned char)r,(unsigned char)g,(unsigned char)b,255);
        return 1;
    }
    if (strncmp(v, "rgb(", 4) == 0 || strncmp(v, "rgba(", 5) == 0) {
        const char *p = strchr(v, '(');
        if (p && sscanf(p + 1, " %u , %u , %u", &r, &g, &b) == 3 &&
            r <= 255 && g <= 255 && b <= 255) {
            *out = color((unsigned char)r,(unsigned char)g,(unsigned char)b,
                         (unsigned char)a);
            return 1;
        }
    }
    if (n == 4 && v[0] == '#' && isxdigit((unsigned char)v[1]) &&
        isxdigit((unsigned char)v[2]) && isxdigit((unsigned char)v[3])) {
        char mini[3];
        mini[2] = '\0';
        mini[0] = v[1]; mini[1] = v[1]; r = (unsigned int)strtoul(mini,NULL,16);
        mini[0] = v[2]; mini[1] = v[2]; g = (unsigned int)strtoul(mini,NULL,16);
        mini[0] = v[3]; mini[1] = v[3]; b = (unsigned int)strtoul(mini,NULL,16);
        *out = color((unsigned char)r,(unsigned char)g,(unsigned char)b,255);
        return 1;
    }
    if (strcmp(v,"transparent") == 0) { *out = color(0,0,0,0); return 1; }
    if (strcmp(v,"white") == 0) { *out = color(255,255,255,255); return 1; }
    if (strcmp(v,"black") == 0) { *out = color(0,0,0,255); return 1; }
    if (strcmp(v,"red") == 0) { *out = color(220,50,50,255); return 1; }
    if (strcmp(v,"blue") == 0) { *out = color(40,100,230,255); return 1; }
    if (strcmp(v,"green") == 0) { *out = color(30,180,100,255); return 1; }
    return 0;
}

/* Property application is intentionally tiny. Lexbor parses *all* CSS,
 * while this first painter understands only the following properties.
 */
static void apply_declaration(const char *name, const char *value,
    unsigned long specificity, int important, unsigned long order, void *ctx)
{
    static const char *names[H2G_PROP_COUNT] = {
        "color", "background-color", "background", "font-size",
        "font-weight", "display", "padding", "margin",
        "width", "height", "max-width", "gap",
        "flex-direction", "min-height", "border-radius", "line-height",
        "position", "clip", "grid-template-columns"
    };
    H2GMatchApply *apply = (H2GMatchApply *)ctx;
    H2GStyle *style = apply->style;
    float v;
    char resolved[2048];
    int i;
    value = resolve_value(apply->app,value,resolved,sizeof(resolved));
    for (i = 0; i < H2G_PROP_COUNT; i++)
        if (strcmp(name, names[i]) == 0) break;
    if (i == H2G_PROP_COUNT) return;
    order += apply->base;
    if (style->specified[i]) {
        if (important < style->imp[i]) return;
        if (important == style->imp[i] && specificity < style->spec[i])
            return;
        if (important == style->imp[i] && specificity == style->spec[i] &&
            order < style->order[i]) return;
    }
    style->specified[i] = 1;
    style->imp[i] = important;
    style->spec[i] = specificity;
    style->order[i] = order;
    switch (i) {
    case 0: (void)parse_color(value, &style->fg); break;
    case 1: case 2: (void)parse_color(value, &style->bg); break;
    case 3:
        if (css_value(value, &v) && v > 5 && v < 180)
            style->size = (int)v;
        break;
    case 4: style->bold = strcmp(value,"bold") == 0 ||
                          strcmp(value,"700") == 0 ||
                          strcmp(value,"800") == 0; break;
    case 5:
        style->display = strcmp(value,"none") == 0 ? 1 :
                         strcmp(value,"flex") == 0 ? 2 :
                         strcmp(value,"grid") == 0 ? 3 : 0;
        break;
    case 6: if (css_value(value,&v) && v >= 0) style->padding=v; break;
    case 7: if (css_value(value,&v)) style->margin=v; break;
    case 8: if (css_value(value,&v)) style->width=v; break;
    case 9: if (css_value(value,&v)) style->height=v; break;
    case 10: if (css_value(value,&v)) style->max_width=v; break;
    case 11: if (css_value(value,&v)) style->gap=v; break;
    case 12: style->flex_column=strcmp(value,"column")==0; break;
    case 13: if (css_value(value,&v) && v>style->height) style->height=v; break;
    case 16:
        style->position = strcmp(value,"absolute")==0 ? 1 :
                          strcmp(value,"fixed")==0 ? 2 : 0;
        break;
    case 17:
        style->clipped = strncmp(value,"rect(",5)==0 ||
                         strncmp(value,"inset(",6)==0;
        break;
    case 18: {
        int count = 0;
        char *end;
        double fixed = strtod(value, &end);
        if (strncmp(value,"repeat(",7)==0 &&
            sscanf(value,"repeat(%d",&count)==1 &&
            count > 0 && count <= 12) {
            style->grid_columns=count;
            style->grid_fixed_first=0;
        } else if (end != value && fixed > 0 && fixed < 10000 &&
                   strncmp(end,"px",2)==0) {
            /* First useful nonuniform track form: a fixed sidebar
             * followed by a fluid main pane, as generated by Tailwind
             * arbitrary grid-template-columns utilities.
             */
            end += 2;
            while (isspace((unsigned char)*end)) end++;
            if (strcmp(end,"minmax(0,1fr)")==0 ||
                strcmp(end,"minmax(0, 1fr)")==0 ||
                strcmp(end,"1fr")==0) {
                style->grid_columns=2;
                style->grid_fixed_first=(float)fixed;
            }
        }
        break;
    }
    default: break;
    }
}

static int editable_input(const H2GDomNode *node);

static H2GStyle style_for(H2GApp *app, const H2GDomNode *node,
                          const H2GStyle *parent)
{
    H2GStyle s;
    H2GMatchApply apply;
    int i;
    memset(&s, 0, sizeof(s));
    s.fg = parent ? parent->fg : color(26,35,53,255);
    s.bg = color(0,0,0,0);
    s.size = parent ? parent->size : 16;
    s.bold = parent ? parent->bold : 0;
    s.width = -1; s.height = -1; s.max_width = -1;
    if (tag_is(node,"body")) s.margin = 8;
    if (tag_is(node,"p")) s.margin = 8;
    if (tag_is(node,"h1")) { s.size=38; s.bold=1; s.margin=12; }
    if (tag_is(node,"h2")) { s.size=28; s.bold=1; s.margin=10; }
    if (tag_is(node,"h3")) { s.size=22; s.bold=1; s.margin=8; }
    if (tag_is(node,"button")) {
        s.padding=11; s.margin=5;
        s.bg=color(42,100,215,255);
        s.fg=color(255,255,255,255);
    }
    if (editable_input(node)) {
        s.padding=10; s.margin=5;
        s.bg=color(255,255,255,255);
        s.fg=color(26,35,53,255);
    }
    apply.style = &s;
    apply.app = app;
    for (i=0; i<app->sheet_count; i++) {
        apply.base = (unsigned long)i * 100000000UL;
        if (!h2g_css_visit_matching_at_width(app->sheets[i],node,
                                    (float)app->viewport_w,apply_declaration,&apply))
            fprintf(stderr,"html2gui: CSS match error\n");
    }
    return s;
}

/* This first editor supports text-like single-line controls only.
 * Checkboxes, radios, file pickers and password fields need specialized UI.
 */
static int editable_input(const H2GDomNode *node)
{
    size_t length;
    const char *type;
    if (!tag_is(node, "input") ||
        h2g_dom_attribute(node, "disabled", NULL) != NULL ||
        h2g_dom_attribute(node, "readonly", NULL) != NULL)
        return 0;
    type = h2g_dom_attribute(node, "type", &length);
    if (type == NULL || length == 0) return 1;
    return (length == 4 && memcmp(type, "text", 4) == 0) ||
           (length == 6 && memcmp(type, "search", 6) == 0) ||
           (length == 5 && memcmp(type, "email", 5) == 0) ||
           (length == 3 && memcmp(type, "tel", 3) == 0) ||
           (length == 3 && memcmp(type, "url", 3) == 0);
}

static H2GInput *get_input(H2GApp *app, const H2GDomNode *node)
{
    H2GInput *input;
    const char *value;
    size_t len;
    int i;
    if (app->inputs == NULL) return NULL;
    for (i=0;i<app->input_count;i++)
        if (app->inputs[i].node == node)
            return &app->inputs[i];
    if (app->input_count >= H2G_MAX_INPUTS) return NULL;
    input = &app->inputs[app->input_count];
    h2g_text_input_init(&input->editor);
    value = h2g_dom_attribute(node, "value", &len);
    if (value == NULL) { value = ""; len = 0; }
    if (!h2g_text_input_set(&input->editor, value, len))
        return NULL;
    input->node = node;
    value = h2g_dom_attribute(node, "id", &len);
    if (value != NULL) {
        if (len >= sizeof(input->id)) len = sizeof(input->id) - 1;
        memcpy(input->id, value, len);
        input->id[len] = '\0';
    }
    app->input_count++;
    return input;
}

static const char *choose_font(void)
{
    static const char *paths[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        NULL
    };
    const char *env = getenv("H2G_FONT");
    FILE *f;
    int i;
    if (env && *env) return env;
    for (i=0; paths[i]; i++) {
        f=fopen(paths[i],"rb");
        if (f) { fclose(f); return paths[i]; }
    }
    return NULL;
}

static TTF_Font *get_font(H2GApp *app, int size, int bold)
{
    TTF_Font *f;
    int i;
    for (i=0; i<app->font_count; i++)
        if (app->fonts[i].size==size && app->fonts[i].bold==bold)
            return app->fonts[i].font;
    if (app->font_count>=H2G_MAX_FONTS) return NULL;
    f=TTF_OpenFont(app->font_path,(float)size);
    if (!f) return NULL;
    if (bold) TTF_SetFontStyle(f,TTF_STYLE_BOLD);
    app->fonts[app->font_count].size=size;
    app->fonts[app->font_count].bold=bold;
    app->fonts[app->font_count].font=f;
    app->font_count++;
    return f;
}

static H2GOp *add_op(H2GApp *app, int type)
{
    H2GOp *op;
    if (app->op_count>=H2G_MAX_OPS) { app->overflow=1; return NULL; }
    op=&app->ops[app->op_count++];
    memset(op,0,sizeof(*op));
    op->type=type;
    return op;
}

static void clear_ops(H2GApp *app)
{
    int i;
    for (i=0; i<app->op_count; i++)
        if (app->ops[i].texture)
            SDL_DestroyTexture(app->ops[i].texture);
    app->op_count=0;
    app->dark_backgrounds=0;
    app->overflow=0;
}

static float add_line(H2GApp *app, const char *line,
                      float x, float y, const H2GStyle *s)
{
    TTF_Font *font;
    SDL_Surface *surface;
    H2GOp *op;
    SDL_Color ink;
    int w=0,h=0;
    if (!line || !*line) return 0;
    font=get_font(app,s->size,s->bold);
    if (!font) return (float)(s->size + 5);
    if (!TTF_GetStringSize(font,line,0,&w,&h))
        return (float)(s->size + 5);
    if (y < (float)app->viewport_h + 100000.0f) {
        op=add_op(app,2);
        if (op) {
            ink.r=s->fg.r; ink.g=s->fg.g; ink.b=s->fg.b; ink.a=s->fg.a;
            surface=TTF_RenderText_Blended(font,line,0,ink);
            if (surface) {
                op->texture=SDL_CreateTextureFromSurface(app->renderer,surface);
                SDL_DestroySurface(surface);
            }
            op->color=s->fg;
            op->rect.x=x; op->rect.y=y;
            op->rect.w=(float)w; op->rect.h=(float)h;
        }
    }
    return (float)(h+4);
}

/* Collapse HTML whitespace and emit word-wrapped, cached texture lines. */
static float layout_text(H2GApp *app, const char *src, size_t length,
                         float x, float y, float width, const H2GStyle *s)
{
    char line[H2G_LINE], word[H2G_LINE], proposal[H2G_LINE];
    size_t i=0, n=0, used=0;
    float total=0;
    int w=0,h=0;
    TTF_Font *font=get_font(app,s->size,s->bold);
    if (!font || !src) return 0;
    if (length>H2G_MAX_TEXT) length=H2G_MAX_TEXT;
    if (width<20) width=20;
    line[0]='\0';
    while (i<length) {
        while (i<length && isspace((unsigned char)src[i])) i++;
        n=0;
        while (i<length && !isspace((unsigned char)src[i])) {
            if (n+1<sizeof(word)) word[n++]=src[i];
            i++;
        }
        if (n==0) continue;
        word[n]='\0';
        used=strlen(line);
        if (used+n+2>=sizeof(proposal)) {
            total+=add_line(app,line,x,y+total,s);
            line[0]='\0'; used=0;
        }
        if (used) {
            memcpy(proposal,line,used);
            proposal[used]=' ';
            memcpy(proposal+used+1,word,n+1);
        } else {
            memcpy(proposal,word,n+1);
        }
        if (!TTF_GetStringSize(font,proposal,0,&w,&h)) w=0;
        if (w>width && used) {
            total+=add_line(app,line,x,y+total,s);
            memcpy(line,word,n+1);
        } else {
            strcpy(line,proposal);
        }
    }
    if (line[0]) total+=add_line(app,line,x,y+total,s);
    return total;
}

/* UTF-8 codepoint navigation over the already validated input buffer. */
static size_t h2g_input_next(const char *text, size_t length, size_t pos)
{
    if (pos >= length) return length;
    pos++;
    while (pos < length &&
           (((unsigned char)text[pos] & 0xc0) == 0x80))
        pos++;
    return pos;
}

static size_t h2g_input_prev(const char *text, size_t pos)
{
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 &&
           (((unsigned char)text[pos] & 0xc0) == 0x80))
        pos--;
    return pos;
}

static int h2g_input_width(TTF_Font *font, const char *data, size_t length)
{
    int width=0, height=0;
    if (font == NULL || length == 0) return 0;
    if (!TTF_GetStringSize(font,data,length,&width,&height)) return 0;
    return width;
}

/* Paint a bounded substring instead of creating enormous SDL textures for
 * long input values. Keep the cursor in view when a field is focused.
 */
static float layout_editable_input(H2GApp *app, const H2GDomNode *node,
                                   H2GInput *input, float x, float y,
                                   float available, const H2GStyle *style)
{
    const char *value=input->editor.text;
    size_t length=input->editor.length;
    size_t cursor=input->editor.cursor;
    size_t start=input->view_start, end, previous, next, visible_length;
    const char *placeholder;
    size_t placeholder_length;
    char visible[H2G_TEXT_INPUT_MAX_BYTES + 1];
    TTF_Font *font=get_font(app,style->size,style->bold);
    H2GOp *caret;
    int room, caret_width=0;
    float used=0;

    input->text_x=x;
    input->font_size=style->size;
    input->font_bold=style->bold;
    room=(int)(available - 4.0f);
    if (room < 8) room=8;
    if (app->focused_input != input || start > cursor)
        start=0;
    if (app->focused_input == input && font != NULL &&
        h2g_input_width(font,value+start,cursor-start)>room) {
        start=cursor;
        while (start > 0) {
            previous=h2g_input_prev(value,start);
            if (h2g_input_width(font,value+previous,
                                cursor-previous)>room)
                break;
            start=previous;
        }
    }
    end=start;
    while (end < length) {
        next=h2g_input_next(value,length,end);
        if (font != NULL &&
            h2g_input_width(font,value+start,next-start)>room)
            break;
        end=next;
    }
    if (end==start && start<length)
        end=h2g_input_next(value,length,start);
    input->view_start=start;
    input->view_end=end;
    visible_length=end-start;
    if (visible_length > 0) {
        memcpy(visible,value+start,visible_length);
        visible[visible_length]='\0';
        used=add_line(app,visible,x,y,style);
    } else {
        H2GStyle dim=*style;
        dim.fg=color(125,138,157,255);
        placeholder=h2g_dom_attribute(node,"placeholder",
                                       &placeholder_length);
        if (placeholder != NULL)
            used=layout_text(app,placeholder,placeholder_length,x,y,
                             available,&dim);
    }

    if (app->focused_input == input) {
        if (cursor < start) cursor=start;
        if (cursor > end) cursor=end;
        caret_width=h2g_input_width(font,value+start,cursor-start);
        if (caret_width>room) caret_width=room;
        caret=add_op(app,1);
        if (caret != NULL) {
            caret->rect.x=x+(float)caret_width;
            caret->rect.y=y+2;
            caret->rect.w=2;
            caret->rect.h=(float)style->size+2;
            caret->color=color(37,99,235,255);
        }
    }
    if (used<=0) used=(float)(style->size+5);
    return used;
}

static int is_text_container(const H2GDomNode *node)
{
    return tag_is(node,"p") || tag_is(node,"h1") || tag_is(node,"h2") ||
           tag_is(node,"h3") || tag_is(node,"h4") || tag_is(node,"h5") ||
           tag_is(node,"h6") || tag_is(node,"span") || tag_is(node,"a") ||
           tag_is(node,"button") || tag_is(node,"label") ||
           tag_is(node,"strong") || tag_is(node,"em") || tag_is(node,"li");
}

static int ignored_element(const H2GDomNode *node)
{
    return tag_is(node,"script") || tag_is(node,"style") ||
           tag_is(node,"meta") || tag_is(node,"title") ||
           tag_is(node,"link") || tag_is(node,"svg") ||
           tag_is(node,"template") || tag_is(node,"noscript");
}

static float layout_node(H2GApp *app, const H2GDomNode *node,
                         const H2GStyle *parent, float x, float y,
                         float available, int depth)
{
    H2GStyle s;
    const H2GDomNode *child;
    H2GOp *background=NULL, *button=NULL, *hit=NULL;
    H2GInput *input=NULL;
    size_t len;
    const char *text, *id;
    float outer_x,outer_y,box_w,inner_w,inner_y,used=0,ch,content_h;
    int children=0,index=0;
    if (depth>=H2G_MAX_DEPTH || app->overflow || !node) return 0;
    if (h2g_dom_type(node)==H2G_DOM_TEXT) {
        text=h2g_dom_text(node,&len);
        return layout_text(app,text,len,x,y,available,parent);
    }
    if (h2g_dom_type(node)!=H2G_DOM_ELEMENT || ignored_element(node))
        return 0;
    s=style_for(app,node,parent);
    if (s.display==1 || s.clipped) return 0;
    if (getenv("H2G_DUMP_LAYOUT") != NULL &&
        (depth==1 || tag_is(node,"h1") || tag_is(node,"main") ||
         tag_is(node,"header") || tag_is(node,"aside"))) {
        size_t tag_len;
        const char *tag = h2g_dom_element_name(node,&tag_len);
        fprintf(stderr,"H2G_LAYOUT %.*s depth=%d x=%.0f y=%.0f avail=%.0f pad=%.0f gap=%.0f size=%d display=%d grid=%d first=%.0f\n",
            (int)tag_len,tag,depth,x,y,available,s.padding,s.gap,s.size,s.display,s.grid_columns,s.grid_fixed_first);
    }
    if (s.padding<0) s.padding=0;
    if (s.margin<0) s.margin=0;
    if (s.gap<0) s.gap=0;
    outer_x=x+s.margin;
    outer_y=y+s.margin;
    if (tag_is(node,"h1") && app->first_h1_y < 0)
        app->first_h1_y=outer_y;
    box_w=available-2*s.margin;
    if (s.width>=0 && s.width+2*s.padding<box_w)
        box_w=s.width+2*s.padding;
    if (s.max_width>=0 && s.max_width<box_w)
        box_w=s.max_width;
    if (box_w<20) box_w=20;
    inner_w=box_w-2*s.padding;
    if (inner_w<10) inner_w=10;
    inner_y=outer_y+s.padding;

    if (s.bg.a) {
        if ((int)s.bg.r+(int)s.bg.g+(int)s.bg.b < 270)
            app->dark_backgrounds++;
        background=add_op(app,1);
        if (background) {
            background->rect.x=outer_x; background->rect.y=outer_y;
            background->rect.w=box_w; background->color=s.bg;
        }
    }
    if (is_text_container(node)) {
        text=h2g_dom_text_content(node,&len);
        used=layout_text(app,text,len,outer_x+s.padding,inner_y,inner_w,&s);
        h2g_dom_text_content_free(node,text);
        if (used<=0) used=(float)(s.size+5);
    } else if (tag_is(node,"input")) {
        if (editable_input(node))
            input=get_input(app,node);
        if (input != NULL) {
            used=layout_editable_input(app,node,input,
                                       outer_x+s.padding,inner_y,
                                       inner_w,&s);
        } else {
            /* Unsupported password controls must never paint the raw
             * value attribute. They are not editable in this slice.
             */
            const char *type;
            size_t type_length=0;
            int password=0;
            type=h2g_dom_attribute(node,"type",&type_length);
            if (type != NULL && type_length == 8 &&
                memcmp(type,"password",8)==0)
                password=1;
            text=h2g_dom_attribute(node,"placeholder",&len);
            if (text == NULL && !password)
                text=h2g_dom_attribute(node,"value",&len);
            if (text) used=layout_text(app,text,len,outer_x+s.padding,
                                       inner_y,inner_w,&s);
        }
        if (used<=0) used=(float)(s.size+5);
    } else if (s.display==3) {
        int cols = s.grid_columns > 0 ? s.grid_columns : 1;
        int col = 0, rows = 0;
        float cell_w, row_max = 0, row_y = inner_y;
        float first_w = 0, second_w = 0, child_w, child_x;
        cell_w = (inner_w - (cols - 1)*s.gap) / cols;
        if (cell_w < 20) cell_w = 20;
        if (cols == 2 && s.grid_fixed_first > 0) {
            first_w = s.grid_fixed_first;
            second_w = inner_w - first_w - s.gap;
            if (second_w < 20) {
                first_w = cell_w;
                second_w = cell_w;
            }
        }
        for (child=h2g_dom_first_child(node); child;
             child=h2g_dom_next_sibling(child)) {
            if (h2g_dom_type(child)!=H2G_DOM_ELEMENT ||
                ignored_element(child)) continue;
            child_w = cell_w;
            child_x = outer_x+s.padding+col*(cell_w+s.gap);
            if (first_w > 0) {
                child_w = col == 0 ? first_w : second_w;
                child_x = outer_x+s.padding+
                          (col == 0 ? 0 : first_w+s.gap);
            }
            ch=layout_node(app,child,&s,child_x,row_y,child_w,depth+1);
            if (ch > row_max) row_max = ch;
            col++;
            if (col == cols) {
                used += row_max + s.gap;
                row_y = inner_y + used;
                row_max = 0;
                col = 0;
                rows++;
            }
        }
        if (col > 0) used += row_max;
        else if (rows > 0) used -= s.gap;
    } else if (s.display==2 && !s.flex_column) {
        float cell_w, max_h=0;
        for (child=h2g_dom_first_child(node);child;
             child=h2g_dom_next_sibling(child))
            if (h2g_dom_type(child)==H2G_DOM_ELEMENT &&
                !ignored_element(child)) children++;
        if (children>0) {
            cell_w=(inner_w-(children-1)*s.gap)/children;
            if (cell_w<20) cell_w=20;
            for (child=h2g_dom_first_child(node);child;
                 child=h2g_dom_next_sibling(child)) {
                if (h2g_dom_type(child)!=H2G_DOM_ELEMENT ||
                    ignored_element(child)) continue;
                ch=layout_node(app,child,&s,outer_x+s.padding+
                            index*(cell_w+s.gap),inner_y,cell_w,depth+1);
                if (ch>max_h) max_h=ch;
                index++;
            }
        }
        used=max_h;
    } else {
        for (child=h2g_dom_first_child(node);child;
             child=h2g_dom_next_sibling(child)) {
            ch=layout_node(app,child,&s,outer_x+s.padding,
                           inner_y+used,inner_w,depth+1);
            if (ch>0) { used+=ch+s.gap; children++; }
        }
        if (children) used-=s.gap;
    }
    content_h=used+2*s.padding;
    if (s.height>content_h) content_h=s.height;
    if (content_h<1) content_h=1;
    if (background) background->rect.h=content_h;

    if (tag_is(node,"button")) {
        button=add_op(app,3);
        if (button) {
            button->rect.x=outer_x; button->rect.y=outer_y;
            button->rect.w=box_w; button->rect.h=content_h;
            id=h2g_dom_attribute(node,"id",&len);
            if (id) {
                if (len>=sizeof(button->id)) len=sizeof(button->id)-1;
                memcpy(button->id,id,len); button->id[len]='\0';
            }
        }
    }
    if (input != NULL &&
        h2g_dom_attribute(node, "disabled", NULL) == NULL) {
        hit=add_op(app,4);
        if (hit != NULL) {
            hit->rect.x=outer_x; hit->rect.y=outer_y;
            hit->rect.w=box_w; hit->rect.h=content_h;
            hit->input=input;
        }
    }
    if (s.position==1 || s.position==2) return 0;
    return content_h+2*s.margin;
}

static void rebuild(H2GApp *app)
{
    H2GStyle initial;
    const H2GDomNode *body=h2g_html_body(app->html);
    clear_ops(app);
    app->first_h1_y=-1;
    memset(&initial,0,sizeof(initial));
    initial.size=16;
    initial.fg=color(22,32,47,255);
    initial.width=-1; initial.height=-1; initial.max_width=-1;
    app->document_h=layout_node(app,body,&initial,0,0,
                                (float)app->viewport_w,0);
    if (app->document_h<(float)app->viewport_h) app->document_h=
        (float)app->viewport_h;
    if (app->scroll>app->document_h-app->viewport_h)
        app->scroll=app->document_h-app->viewport_h;
    fprintf(stderr,"html2gui: viewport=%dx%d ops=%d document=%.0f vars=%d dark-bg=%d h1-y=%.0f%s\n",
      app->viewport_w,app->viewport_h,app->op_count,app->document_h,
      app->var_count,app->dark_backgrounds,app->first_h1_y,app->overflow?" (paint buffer exhausted)":"");
    if (getenv("H2G_DUMP_LAYOUT") != NULL) {
        int i, printed = 0;
        for (i=0; i<app->op_count && printed<75; i++) {
            const H2GOp *op=&app->ops[i];
            if (op->type!=2) continue;
            fprintf(stderr,"H2G_TEXT x=%.0f y=%.0f w=%.0f h=%.0f rgba=%u,%u,%u,%u\n",
              op->rect.x,op->rect.y,op->rect.w,op->rect.h,
              (unsigned)op->color.r,(unsigned)op->color.g,
              (unsigned)op->color.b,(unsigned)op->color.a);
            printed++;
        }
    }
}

static void draw(H2GApp *app)
{
    int i;
    H2GOp *op;
    SDL_FRect dest;
    SDL_SetRenderDrawColor(app->renderer,247,249,252,255);
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawBlendMode(app->renderer,SDL_BLENDMODE_BLEND);
    for (i=0;i<app->op_count;i++) {
        op=&app->ops[i];
        if (op->type==3 || op->type==4) continue;
        dest=op->rect;
        dest.y-=app->scroll;
        if (dest.y+dest.h<0 || dest.y>app->viewport_h) continue;
        if (op->type==1) {
            SDL_SetRenderDrawColor(app->renderer,op->color.r,op->color.g,
                op->color.b,op->color.a);
            SDL_RenderFillRect(app->renderer,&dest);
        } else if (op->type==2 && op->texture) {
            SDL_RenderTexture(app->renderer,op->texture,NULL,&dest);
        }
    }
    {
        const char *screenshot = getenv("H2G_SCREENSHOT");
        if (screenshot != NULL && *screenshot != '\0') {
            SDL_Surface *pixels = SDL_RenderReadPixels(app->renderer, NULL);
            if (pixels != NULL) {
                if (!SDL_SaveBMP(pixels, screenshot))
                    fprintf(stderr,"html2gui: screenshot save failed: %s\n",
                            SDL_GetError());
                SDL_DestroySurface(pixels);
            } else {
                fprintf(stderr,"html2gui: screen read failed: %s\n",
                        SDL_GetError());
            }
        }
    }
    SDL_RenderPresent(app->renderer);
}

static void notify_input(H2GApp *app, H2GInput *input)
{
    if (app->on_input != NULL)
        app->on_input(input->id, input->editor.text, app->callback_user);
}

static int focus_input(H2GApp *app, H2GInput *input)
{
    if (app->focused_input == input) return 0;
    if (app->focused_input != NULL)
        (void)SDL_StopTextInput(app->window);
    app->focused_input=input;
    if (input != NULL)
        (void)SDL_StartTextInput(app->window);
    return 1;
}

static int focus_next_input(H2GApp *app, int backwards)
{
    int i;
    if (app->input_count == 0) return 0;
    if (app->focused_input == NULL) {
        i=backwards ? app->input_count-1 : 0;
    } else {
        i=(int)(app->focused_input-app->inputs);
        i=backwards ? (i+app->input_count-1)%app->input_count :
                      (i+1)%app->input_count;
    }
    return focus_input(app,&app->inputs[i]);
}

/* Put the insertion caret at the nearest visible UTF-8 boundary. */
static size_t h2g_input_position_at_x(H2GApp *app, H2GInput *input,
                                      float x)
{
    TTF_Font *font;
    const char *text=input->editor.text;
    size_t start=input->view_start, position=start, next;
    float relative=x-input->text_x;
    int previous_width=0, next_width;
    if (relative<=0) return start;
    font=get_font(app,input->font_size,input->font_bold);
    while (position<input->view_end) {
        next=h2g_input_next(text,input->editor.length,position);
        next_width=h2g_input_width(font,text+start,next-start);
        if (relative < ((float)previous_width+(float)next_width)*0.5f)
            return position;
        position=next;
        previous_width=next_width;
    }
    return input->view_end;
}

static int click(H2GApp *app, float x, float y)
{
    int i;
    H2GOp *op;
    y+=app->scroll;
    for (i=app->op_count-1;i>=0;i--) {
        op=&app->ops[i];
        if ((op->type==3 || op->type==4) &&
            x>=op->rect.x && x<op->rect.x+op->rect.w &&
            y>=op->rect.y && y<op->rect.y+op->rect.h) {
            if (op->type==4) {
                if (op->input != NULL)
                    op->input->editor.cursor=
                        h2g_input_position_at_x(app,op->input,x);
                (void)focus_input(app,op->input);
                return 1;
            }
            (void)focus_input(app,NULL);
            if (app->on_button != NULL) {
                app->on_button(op->id, app->callback_user);
            } else {
                printf("html2gui: button clicked id=%s\n",
                       op->id[0] ? op->id : "(unnamed)");
                fflush(stdout);
            }
            return 1;
        }
    }
    return focus_input(app,NULL);
}

static int handle_text_key(H2GApp *app, SDL_Keycode key)
{
    H2GInput *input=app->focused_input;
    int changed=0, moved=0;
    if (input == NULL) return 0;
    if (key == SDLK_BACKSPACE)
        changed=h2g_text_input_backspace(&input->editor);
    else if (key == SDLK_DELETE)
        changed=h2g_text_input_delete(&input->editor);
    else if (key == SDLK_LEFT)
        moved=h2g_text_input_left(&input->editor);
    else if (key == SDLK_RIGHT)
        moved=h2g_text_input_right(&input->editor);
    else if (key == SDLK_HOME) {
        h2g_text_input_home(&input->editor);
        moved=1;
    } else if (key == SDLK_END) {
        h2g_text_input_end(&input->editor);
        moved=1;
    }
    if (changed) notify_input(app,input);
    return changed || moved;
}

static int handle_text_insert(H2GApp *app, const char *text)
{
    H2GInput *input=app->focused_input;
    if (input == NULL || text == NULL) return 0;
    if (!h2g_text_input_insert(&input->editor,text,strlen(text)))
        return 0;
    notify_input(app,input);
    return 1;
}

static int add_css(H2GApp *app, const char *data, size_t length)
{
    H2GCssStylesheet *sheet;
    if (app->sheet_count>=H2G_MAX_SHEETS) return 0;
    sheet=h2g_css_parse(data,length);
    if (!sheet) return 0;
    fprintf(stderr,"html2gui: stylesheet rules=%lu\n",
            (unsigned long)h2g_css_indexed_style_rule_count(sheet));
    app->sheets[app->sheet_count++]=sheet;
    if (h2g_html_head(app->html) != NULL) {
        const H2GDomNode *root = h2g_dom_parent(h2g_html_head(app->html));
        if (root != NULL)
            (void)h2g_css_visit_matching(sheet,root,capture_var,app);
        (void)h2g_css_visit_root_variables(sheet,capture_var,app);
    }
    return 1;
}

static void scan_styles(H2GApp *app, const H2GDomNode *node, int depth)
{
    const H2GDomNode *child;
    const char *text;
    size_t len;
    if (depth>32 || !node || app->sheet_count>=H2G_MAX_SHEETS) return;
    if (tag_is(node,"style")) {
        text=h2g_dom_text_content(node,&len);
        if (text && len) (void)add_css(app,text,len);
        h2g_dom_text_content_free(node,text);
        return;
    }
    for (child=h2g_dom_first_child(node);child;
         child=h2g_dom_next_sibling(child))
        scan_styles(app,child,depth+1);
}

int h2g_renderer_run_ex(const char *html_path, const char *css_path,
                        int headless, H2GButtonCallback on_button,
                        H2GInputCallback on_input, void *user)
{
    H2GApp app;
    SDL_Event ev;
    char *html_data,*css_data;
    size_t length;
    int again=1,i;
    float high;
    memset(&app,0,sizeof(app));
    app.on_button=on_button;
    app.on_input=on_input;
    app.callback_user=user;
    if (html_path==NULL) {
        fprintf(stderr,"html2gui: HTML path is required\n");
        return 2;
    }

    html_data=read_file(html_path,&length);
    if (!html_data) { fprintf(stderr,"cannot read %s\n",html_path); return 1; }
    app.html=h2g_html_parse(html_data,length);
    free(html_data);
    if (!app.html) { fprintf(stderr,"HTML parse failed\n"); return 1; }
    app.vars=(H2GVariable *)calloc(H2G_MAX_VARS,sizeof(H2GVariable));
    if (app.vars == NULL) {
        fprintf(stderr,"out of memory allocating CSS variables\n");
        return 1;
    }
    app.inputs=(H2GInput *)calloc(H2G_MAX_INPUTS,sizeof(H2GInput));
    if (app.inputs == NULL) {
        fprintf(stderr,"out of memory allocating input states\n");
        return 1;
    }
    scan_styles(&app,h2g_html_head(app.html),0);
    if (css_path) {
        css_data=read_file(css_path,&length);
        if (!css_data) {
            fprintf(stderr,"cannot read %s\n",css_path); return 1;
        }
        if (!add_css(&app,css_data,length))
            fprintf(stderr,"warning: CSS parsing failed\n");
        free(css_data);
    }
    app.font_path=choose_font();
    if (!app.font_path) {
        fprintf(stderr,"No font. Set H2G_FONT=/path/to/font.ttf\n");
        return 1;
    }
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
    }
    if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
        fprintf(stderr,"SDL init: %s\n",SDL_GetError()); return 1;
    }
    if (!SDL_CreateWindowAndRenderer("html2gui - SDL3",1100,750,
          headless?SDL_WINDOW_HIDDEN:SDL_WINDOW_RESIZABLE,
          &app.window,&app.renderer)) {
        fprintf(stderr,"SDL renderer init: %s\n",SDL_GetError()); return 1;
    }
    app.viewport_w=1100; app.viewport_h=750;
    app.ops=(H2GOp *)calloc(H2G_MAX_OPS,sizeof(H2GOp));
    if (!app.ops) { fprintf(stderr,"out of memory\n"); return 1; }
    rebuild(&app);
    draw(&app);
    if (headless && getenv("H2G_TORTURE_VERIFY") != NULL) {
        if (app.op_count < 1000 || app.document_h < 10000 ||
            app.var_count < 100 || app.dark_backgrounds < 100 ||
            app.first_h1_y < 0 || app.first_h1_y >= app.viewport_h ||
            app.overflow) {
            fprintf(stderr,
              "html2gui: TORTURE VERIFY FAIL ops=%d doc=%.0f vars=%d dark=%d overflow=%d\n",
              app.op_count, app.document_h, app.var_count,
              app.dark_backgrounds, app.overflow);
            return 1;
        }
        fprintf(stderr,"html2gui: TORTURE VERIFY PASS\n");
    }
    /* Opt-in synthetic hit-test for the CI callback smoke test only.
     * Never inject clicks in a normal interactive application.
     */
    if (headless && getenv("H2G_TEST_CLICK") != NULL) {
        const char *target = getenv("H2G_TEST_CLICK");
        for (i=0;i<app.op_count;i++) {
            H2GOp *op=&app.ops[i];
            if (op->type==3 && strcmp(op->id,target)==0) {
                click(&app,op->rect.x+op->rect.w*0.5f,
                           op->rect.y+op->rect.h*0.5f-app.scroll);
                break;
            }
        }
    }
    /* A controlled headless edit exercises the same click/edit dispatch
     * as the SDL event path, without a window manager or keyboard.
     */
    if (headless && getenv("H2G_TEST_INPUT") != NULL) {
        const char *target = getenv("H2G_TEST_INPUT");
        const char *text = getenv("H2G_TEST_TEXT");
        for (i=0;i<app.op_count;i++) {
            H2GOp *op=&app.ops[i];
            if (op->type==4 && op->input != NULL &&
                strcmp(op->input->id,target)==0) {
                const char *where=getenv("H2G_TEST_INPUT_X");
                double fraction=where!=NULL ? strtod(where,NULL) : 0.5;
                if (fraction < 0.01) fraction=0.01;
                if (fraction > 0.99) fraction=0.99;
                (void)click(&app,op->rect.x+op->rect.w*(float)fraction,
                            op->rect.y+op->rect.h*0.5f-app.scroll);
                if (text != NULL) (void)handle_text_insert(&app,text);
                rebuild(&app);
                draw(&app);
                break;
            }
        }
    }
    if (!headless) while (again && SDL_WaitEvent(&ev)) {
        if (ev.type==SDL_EVENT_QUIT) again=0;
        else if (ev.type==SDL_EVENT_WINDOW_RESIZED) {
            if (SDL_GetWindowSize(app.window,&app.viewport_w,&app.viewport_h))
                rebuild(&app);
            draw(&app);
        } else if (ev.type==SDL_EVENT_MOUSE_WHEEL) {
            app.scroll-=(float)ev.wheel.y*56.0f;
            high=app.document_h-app.viewport_h;
            if (app.scroll<0) app.scroll=0;
            if (app.scroll>high) app.scroll=high;
            draw(&app);
        } else if (ev.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (click(&app,ev.button.x,ev.button.y))
                rebuild(&app);
            draw(&app);
        } else if (ev.type==SDL_EVENT_TEXT_INPUT) {
            if (handle_text_insert(&app,ev.text.text)) {
                rebuild(&app);
                draw(&app);
            }
        } else if (ev.type==SDL_EVENT_KEY_DOWN) {
            int redraw=0;
            if (ev.key.key==SDLK_ESCAPE) {
                if (app.focused_input != NULL)
                    redraw=focus_input(&app,NULL);
                else again=0;
            } else if (ev.key.key==SDLK_TAB) {
                redraw=focus_next_input(&app,
                         (ev.key.mod & SDL_KMOD_SHIFT) != 0);
            } else if (ev.key.key==SDLK_RETURN ||
                       ev.key.key==SDLK_KP_ENTER) {
                redraw=focus_input(&app,NULL);
            } else {
                redraw=handle_text_key(&app,ev.key.key);
            }
            if (ev.key.key==SDLK_PAGEDOWN) {
                app.scroll+=app.viewport_h*.8f;
                if (app.scroll>app.document_h-app.viewport_h)
                    app.scroll=app.document_h-app.viewport_h;
                draw(&app);
            }
            if (ev.key.key==SDLK_PAGEUP) {
                app.scroll-=app.viewport_h*.8f;
                if (app.scroll<0) app.scroll=0;
                draw(&app);
            }
            if (redraw) {
                rebuild(&app);
                draw(&app);
            }
        } else if (ev.type==SDL_EVENT_WINDOW_EXPOSED) draw(&app);
    }
    if (app.focused_input != NULL)
        (void)SDL_StopTextInput(app.window);
    clear_ops(&app);
    free(app.ops);
    for (i=0;i<app.font_count;i++) TTF_CloseFont(app.fonts[i].font);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    TTF_Quit();
    SDL_Quit();
    for (i=0;i<app.sheet_count;i++) h2g_css_destroy(app.sheets[i]);
    for (i=0;i<app.var_count;i++) {
        free(app.vars[i].name);
        free(app.vars[i].value);
    }
    free(app.vars);
    for (i=0;i<app.input_count;i++)
        h2g_text_input_destroy(&app.inputs[i].editor);
    free(app.inputs);
    h2g_html_destroy(app.html);
    return 0;
}

int h2g_renderer_run(const char *html_path, const char *css_path,
                     int headless, H2GButtonCallback on_button, void *user)
{
    return h2g_renderer_run_ex(html_path,css_path,headless,on_button,
                               NULL,user);
}
