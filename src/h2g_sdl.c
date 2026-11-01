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
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define H2G_MAX_OPS 60000
#define H2G_MAX_FONTS 32
#define H2G_MAX_SHEETS 16
#define H2G_MAX_DEPTH 128
#define H2G_MAX_TEXT 32768
#define H2G_LINE 4096
#define H2G_PROP_COUNT 16

typedef struct H2GColor { unsigned char r, g, b, a; } H2GColor;
typedef struct H2GStyle {
    H2GColor fg, bg;
    int size, bold, display, flex_column;
    float margin, padding, gap, width, height, max_width;
    int specified[H2G_PROP_COUNT];
    int imp[H2G_PROP_COUNT];
    unsigned long spec[H2G_PROP_COUNT], order[H2G_PROP_COUNT];
} H2GStyle;
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
    H2GOp *ops;
    int op_count, overflow, viewport_w, viewport_h;
    float document_h, scroll;
} H2GApp;
typedef struct H2GMatchApply {
    H2GStyle *style;
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

static int parse_color(const char *v, H2GColor *out)
{
    unsigned int r, g, b, a = 255;
    size_t n = strlen(v);
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
        "flex-direction", "min-height", "border-radius", "line-height"
    };
    H2GMatchApply *apply = (H2GMatchApply *)ctx;
    H2GStyle *style = apply->style;
    float v;
    int i;
    (void)order;
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
                         strcmp(value,"flex") == 0 ? 2 : 0;
        break;
    case 6: if (css_value(value,&v) && v >= 0) style->padding=v; break;
    case 7: if (css_value(value,&v)) style->margin=v; break;
    case 8: if (css_value(value,&v)) style->width=v; break;
    case 9: if (css_value(value,&v)) style->height=v; break;
    case 10: if (css_value(value,&v)) style->max_width=v; break;
    case 11: if (css_value(value,&v)) style->gap=v; break;
    case 12: style->flex_column=strcmp(value,"column")==0; break;
    case 13: if (css_value(value,&v) && v>style->height) style->height=v; break;
    default: break;
    }
}

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
    apply.style = &s;
    for (i=0; i<app->sheet_count; i++) {
        apply.base = (unsigned long)i * 100000000UL;
        if (!h2g_css_visit_matching(app->sheets[i],node,
                                    apply_declaration,&apply))
            fprintf(stderr,"html2gui: CSS match error\n");
    }
    return s;
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
    H2GOp *background=NULL, *button=NULL;
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
    if (s.display==1) return 0;
    if (s.padding<0) s.padding=0;
    if (s.margin<0) s.margin=0;
    if (s.gap<0) s.gap=0;
    outer_x=x+s.margin;
    outer_y=y+s.margin;
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
        text=h2g_dom_attribute(node,"placeholder",&len);
        if (!text) text=h2g_dom_attribute(node,"value",&len);
        if (text) used=layout_text(app,text,len,outer_x+s.padding,inner_y,
                                   inner_w,&s);
        if (used<=0) used=(float)(s.size+5);
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
    return content_h+2*s.margin;
}

static void rebuild(H2GApp *app)
{
    H2GStyle initial;
    const H2GDomNode *body=h2g_html_body(app->html);
    clear_ops(app);
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
    fprintf(stderr,"html2gui: viewport=%dx%d ops=%d document=%.0f%s\n",
      app->viewport_w,app->viewport_h,app->op_count,app->document_h,
      app->overflow?" (paint buffer exhausted)":"");
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
        if (op->type==3) continue;
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
    SDL_RenderPresent(app->renderer);
}

static void click(H2GApp *app, float x, float y)
{
    int i;
    H2GOp *op;
    y+=app->scroll;
    for (i=app->op_count-1;i>=0;i--) {
        op=&app->ops[i];
        if (op->type==3 &&
            x>=op->rect.x && x<op->rect.x+op->rect.w &&
            y>=op->rect.y && y<op->rect.y+op->rect.h) {
            printf("html2gui: button clicked id=%s\n",
                   op->id[0]?op->id:"(unnamed)");
            fflush(stdout);
            return;
        }
    }
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

int main(int argc, char **argv)
{
    H2GApp app;
    SDL_Event ev;
    char *html_data,*css_data;
    size_t length;
    const char *html_path,*css_path=NULL;
    int headless=0,again=1,i;
    float high;
    memset(&app,0,sizeof(app));
    if (argc<2) {
        fprintf(stderr,"usage: %s [--headless] page.html [style.css]\n",
                argv[0]);
        return 2;
    }
    i=1;
    if (strcmp(argv[i],"--headless")==0) { headless=1; i++; }
    if (argc-i<1 || argc-i>2) {
        fprintf(stderr,"usage: %s [--headless] page.html [style.css]\n",
                argv[0]);
        return 2;
    }
    html_path=argv[i++];
    if (i<argc) css_path=argv[i];

    html_data=read_file(html_path,&length);
    if (!html_data) { fprintf(stderr,"cannot read %s\n",html_path); return 1; }
    app.html=h2g_html_parse(html_data,length);
    free(html_data);
    if (!app.html) { fprintf(stderr,"HTML parse failed\n"); return 1; }
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
            click(&app,ev.button.x,ev.button.y);
        } else if (ev.type==SDL_EVENT_KEY_DOWN) {
            if (ev.key.key==SDLK_ESCAPE) again=0;
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
        } else if (ev.type==SDL_EVENT_WINDOW_EXPOSED) draw(&app);
    }
    clear_ops(&app);
    free(app.ops);
    for (i=0;i<app.font_count;i++) TTF_CloseFont(app.fonts[i].font);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    TTF_Quit();
    SDL_Quit();
    for (i=0;i<app.sheet_count;i++) h2g_css_destroy(app.sheets[i]);
    h2g_html_destroy(app.html);
    return 0;
}
