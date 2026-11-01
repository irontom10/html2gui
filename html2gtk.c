/*
 * html2gtk - intentionally small HTML/Tailwind -> GtkBuilder compiler.
 * Strict ISO C89, no parser libraries, no runtime GTK dependency.
 * License: MIT.  This is a constrained HTML dialect, not a browser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_NODES 20000
#define MAX_DEPTH 256
#define MAX_TEXT 262144
#define MAX_SOURCE (16L * 1024L * 1024L)
#define MAX_ATTRS 32

typedef struct Attr {
    char *name;
    char *value;
} Attr;

typedef struct Node {
    char *tag;
    char *text;
    Attr attrs[MAX_ATTRS];
    int attr_count;
    int parent, first, last, next;
    int line;
} Node;

static Node nodes[MAX_NODES];
static int count = 0, warnings = 0;
static FILE *xmlout, *cssout;
static const char *source_name;
static int viewport_width = 1100;

/* Intern identical Tailwind class sets. GTK style matching costs more than
 * the source CSS size suggests, so don't install duplicate or empty rules.
 * Owners are node indexes; zero means there is no stylesheet class.
 */
#define STYLE_TABLE_SIZE 32768
static int style_owner[MAX_NODES];
static int style_table[STYLE_TABLE_SIZE];
static unsigned char style_has_css[MAX_NODES];
static FILE *css_stage;

static unsigned long style_hash(const char *s)
{
    unsigned long h = 2166136261UL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }
    return h;
}


static void fatal(const char *s)
{
    fprintf(stderr, "html2gtk: fatal: %s\n", s);
    exit(1);
}

static void warn_at(int n, const char *fmt, ...)
{
    va_list ap;
    if (warnings < 100) {
        fprintf(stderr, "%s:%d: warning: ", source_name,
                n >= 0 ? nodes[n].line : 1);
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }
    warnings++;
}

static void *xalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) fatal("out of memory");
    return p;
}

static char *dup_range(const char *p, size_t n)
{
    char *q = (char *)xalloc(n + 1);
    memcpy(q, p, n);
    q[n] = '\0';
    return q;
}

static char *dup_s(const char *s)
{
    return dup_range(s, strlen(s));
}

static int eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static int begins(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int whitespace_only(const char *s)
{
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

static void xml_escape(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p == '&') fputs("&amp;", f);
        else if (*p == '<') fputs("&lt;", f);
        else if (*p == '>') fputs("&gt;", f);
        else if (*p == '\"') fputs("&quot;", f);
        else if (*p == '\'') fputs("&apos;", f);
        else fputc(*p, f);
        p++;
    }
}

static void emit_utf8(char *out, size_t *k, unsigned long c, size_t lim)
{
    unsigned char b[4];
    int i, n;
    if (c < 0x80UL) { b[0] = (unsigned char)c; n = 1; }
    else if (c < 0x800UL) {
        b[0] = (unsigned char)(0xc0 | (c >> 6));
        b[1] = (unsigned char)(0x80 | (c & 63)); n = 2;
    } else if (c < 0x10000UL) {
        b[0] = (unsigned char)(0xe0 | (c >> 12));
        b[1] = (unsigned char)(0x80 | ((c >> 6) & 63));
        b[2] = (unsigned char)(0x80 | (c & 63)); n = 3;
    } else if (c <= 0x10ffffUL) {
        b[0] = (unsigned char)(0xf0 | (c >> 18));
        b[1] = (unsigned char)(0x80 | ((c >> 12) & 63));
        b[2] = (unsigned char)(0x80 | ((c >> 6) & 63));
        b[3] = (unsigned char)(0x80 | (c & 63)); n = 4;
    } else return;
    for (i = 0; i < n && *k + 1 < lim; i++) out[(*k)++] = (char)b[i];
}

static char *html_decode(const char *src)
{
    size_t len = strlen(src), i = 0, j = 0, stop;
    unsigned long val;
    char *end;
    char *dst = (char *)xalloc(len + 1);
    while (i < len) {
        if (src[i] == '&') {
            stop = i + 1;
            while (stop < len && stop < i + 15 && src[stop] != ';' &&
                   src[stop] != ' ' && src[stop] != '<') stop++;
            if (stop < len && src[stop] == ';') {
                char *entity = dup_range(src + i + 1, stop - i - 1);
                int known = 1;
                if (eq(entity, "amp")) val = 38;
                else if (eq(entity, "lt")) val = 60;
                else if (eq(entity, "gt")) val = 62;
                else if (eq(entity, "quot")) val = 34;
                else if (eq(entity, "apos") || eq(entity, "#39")) val = 39;
                else if (eq(entity, "nbsp")) val = 32;
                else if (eq(entity, "ndash")) val = 8211;
                else if (eq(entity, "mdash")) val = 8212;
                else if (eq(entity, "hellip")) val = 8230;
                else if (entity[0] == '#') {
                    if (entity[1] == 'x' || entity[1] == 'X')
                        val = strtoul(entity + 2, &end, 16);
                    else val = strtoul(entity + 1, &end, 10);
                    if (*end != '\0' || val == 0 || val > 0x10ffffUL ||
                        (val >= 0xd800UL && val <= 0xdfffUL)) known = 0;
                } else known = 0;
                free(entity);
                if (known) {
                    emit_utf8(dst, &j, val, len + 1);
                    i = stop + 1;
                    continue;
                }
            }
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return dst;
}

static int add_node(const char *tag, int parent, int line)
{
    int n;
    if (count == MAX_NODES) fatal("too many nodes (MAX_NODES)");
    n = count++;
    memset(&nodes[n], 0, sizeof(nodes[n]));
    nodes[n].tag = dup_s(tag);
    nodes[n].parent = parent;
    nodes[n].first = nodes[n].last = nodes[n].next = -1;
    nodes[n].line = line;
    if (parent >= 0) {
        if (nodes[parent].first < 0) nodes[parent].first = n;
        else nodes[nodes[parent].last].next = n;
        nodes[parent].last = n;
    }
    return n;
}

static void add_attr(int n, const char *name, const char *value)
{
    int k = nodes[n].attr_count;
    if (k >= MAX_ATTRS) { warn_at(n, "too many attributes on <%s>", nodes[n].tag); return; }
    nodes[n].attrs[k].name = dup_s(name);
    nodes[n].attrs[k].value = html_decode(value);
    nodes[n].attr_count++;
}

static const char *attr(int n, const char *name)
{
    int i;
    for (i = 0; i < nodes[n].attr_count; i++)
        if (eq(nodes[n].attrs[i].name, name)) return nodes[n].attrs[i].value;
    return "";
}

static void intern_styles(void)
{
    int n, owner;
    unsigned long slot;
    const char *classes;
    for (n = 1; n < count; n++) {
        classes = attr(n, "class");
        if (!*classes || eq(nodes[n].tag, "#text")) continue;
        slot = style_hash(classes) % STYLE_TABLE_SIZE;
        while (style_table[slot]) {
            owner = style_table[slot];
            if (eq(attr(owner, "class"), classes)) break;
            slot = (slot + 1) % STYLE_TABLE_SIZE;
        }
        if (!style_table[slot]) style_table[slot] = n;
        style_owner[n] = style_table[slot];
    }
}


static int has_attr(int n, const char *name)
{
    int i;
    for (i = 0; i < nodes[n].attr_count; i++)
        if (eq(nodes[n].attrs[i].name, name)) return 1;
    return 0;
}

static int parse_int(const char *s, int default_value)
{
    char *end;
    long v;
    if (!*s) return default_value;
    v = strtol(s, &end, 10);
    if (*end || v < 0 || v > 10000) return default_value;
    return (int)v;
}

static int tw_units(const char *s)
{
    char *end;
    double value;
    if (eq(s, "px")) return 1;
    value = strtod(s, &end);
    if (end == s || *end || value < 0.0 || value > 1000.0) return -1;
    return (int)(value * 4.0 + 0.5);
}

/* Breakpoints are resolved at compile time, NOT through GTK CSS media queries. */
static const int breakpoints[] = {0, 640, 768, 1024, 1280, 1536};

/* Returns 1 on a recognized token, 0 on an unrecognized variant. */
static int split_utility(const char *word, int *bp, const char **state,
                         const char **utility)
{
    const char *p = word, *colon;
    int i;
    *bp = 0;
    *state = "";
    *utility = word;
    for (i = 0; i < 2; i++) {
        colon = strchr(p, ':');
        if (!colon) break;
        if (colon == p + 2 && strncmp(p, "sm", 2) == 0) *bp = 640;
        else if (colon == p + 2 && strncmp(p, "md", 2) == 0) *bp = 768;
        else if (colon == p + 2 && strncmp(p, "lg", 2) == 0) *bp = 1024;
        else if (colon == p + 2 && strncmp(p, "xl", 2) == 0) *bp = 1280;
        else if (colon == p + 3 && strncmp(p, "2xl", 3) == 0) *bp = 1536;
        else if (colon == p + 5 && strncmp(p, "hover", 5) == 0) *state = "hover";
        else if (colon == p + 5 && strncmp(p, "focus", 5) == 0) *state = "focus";
        else if (colon == p + 11 && strncmp(p, "placeholder", 11) == 0) *state = "placeholder";
        else return 0;
        p = colon + 1;
        *utility = p;
    }
    if (strchr(p, ':')) return 0;
    return 1;
}

/* Active at selected viewport; pseudo states are not layout properties. */
static int class_active(int n, const char *wanted)
{
    const char *p = attr(n, "class"), *start, *utility, *state;
    char *word;
    int bp, match = 0;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p == start) continue;
        word = dup_range(start, (size_t)(p - start));
        if (split_utility(word, &bp, &state, &utility) &&
            bp <= viewport_width && !*state && eq(utility, wanted)) match = 1;
        free(word);
        if (match) break;
    }
    return match;
}

static int word_level(const char *word, int level, const char **state,
                      const char **utility)
{
    int bp;
    if (!split_utility(word, &bp, state, utility)) return 0;
    return bp == level && bp <= viewport_width;
}

static int pixel_value(const char *s)
{
    const char *end;
    char tmp[32];
    long value;
    if (*s == '[') {
        end = strchr(s, ']');
        if (!end || end[1] || end - s < 5 || end - s > 20 ||
            strncmp(end - 2, "px", 2) != 0) return -1;
        if ((size_t)(end - s - 3) >= sizeof(tmp)) return -1;
        memcpy(tmp, s + 1, (size_t)(end - s - 3));
        tmp[end - s - 3] = '\0';
        if (!*tmp) return -1;
        {
            char *parse_end;
            value = strtol(tmp, &parse_end, 10);
            if (*parse_end || value < 0 || value > 20000) return -1;
        }
        return (int)value;
    }
    return tw_units(s);
}

/* GTK CSS has no max-width/max-height. Resolve Tailwind's maximums into
 * native GtkScrolledWindow maximum-content measurements at compile time.
 * The wrapper caps its natural size and clips/scrolls overflowing content.
 * This is not browser min/max-content sizing, but it is a real GTK limit.
 */
static int named_max_width(const char *value)
{
    static const struct { const char *name; int width; } names[] = {
        {"3xs", 256}, {"2xs", 288}, {"xs", 320},
        {"sm", 384}, {"md", 448}, {"lg", 512}, {"xl", 576},
        {"2xl", 672}, {"3xl", 768}, {"4xl", 896},
        {"5xl", 1024}, {"6xl", 1152}, {"7xl", 1280},
        {"min", -1}, {"max", -1}, {"fit", -1},
        {NULL, 0}
    };
    int i;
    if (eq(value, "full") || eq(value, "none") || eq(value, "auto")) return -1;
    if (eq(value, "screen")) return viewport_width;
    for (i = 0; names[i].name; i++)
        if (eq(value, names[i].name)) return names[i].width;
    return pixel_value(value);
}

static int max_dimension(int n, char axis)
{
    const char *p = attr(n, "class"), *start, *state, *utility;
    const char *prefix = axis == 'w' ? "max-w-" : "max-h-";
    int result = -1, bp, v, best_bp = -1;
    char *word;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p == start) continue;
        word = dup_range(start, (size_t)(p - start));
        if (split_utility(word, &bp, &state, &utility) &&
            bp <= viewport_width && !*state && begins(utility, prefix)) {
            if (axis == 'w') v = named_max_width(utility + 6);
            else if (eq(utility + 6, "screen")) v = 750;
            else if (eq(utility + 6, "full") || eq(utility + 6, "none") ||
                     eq(utility + 6, "auto")) v = -1;
            else v = pixel_value(utility + 6);
            if (bp >= best_bp) { best_bp = bp; result = v; }
        }
        free(word);
    }
    return result;
}

static void count_lines(const char *start, const char *end, int *line)
{
    while (start < end) if (*start++ == '\n') (*line)++;
}

static int void_tag(const char *tag)
{
    return eq(tag, "input") || eq(tag, "img") || eq(tag, "br") ||
           eq(tag, "hr") || eq(tag, "meta") || eq(tag, "link") ||
           eq(tag, "source") || eq(tag, "wbr") || eq(tag, "area") ||
           eq(tag, "base") || eq(tag, "col") || eq(tag, "embed") ||
           eq(tag, "param") || eq(tag, "track");
}

/*
 * HTML is not XML.  Several elements have optional end tags, especially
 * <option>, <li>, <p>, and table cells.  Keep the parse stack consistent
 * with the HTML tree instead of requiring XML-style explicit closing tags.
 */
static int optional_end_tag(const char *tag)
{
    return eq(tag, "option") || eq(tag, "optgroup") ||
           eq(tag, "li") || eq(tag, "dt") || eq(tag, "dd") ||
           eq(tag, "p") || eq(tag, "rt") || eq(tag, "rp") ||
           eq(tag, "tr") || eq(tag, "td") || eq(tag, "th") ||
           eq(tag, "thead") || eq(tag, "tbody") || eq(tag, "tfoot");
}

static int opens_block_that_closes_p(const char *tag)
{
    return eq(tag, "address") || eq(tag, "article") || eq(tag, "aside") ||
           eq(tag, "blockquote") || eq(tag, "details") || eq(tag, "div") ||
           eq(tag, "dl") || eq(tag, "fieldset") || eq(tag, "figcaption") ||
           eq(tag, "figure") || eq(tag, "footer") || eq(tag, "form") ||
           eq(tag, "h1") || eq(tag, "h2") || eq(tag, "h3") ||
           eq(tag, "h4") || eq(tag, "h5") || eq(tag, "h6") ||
           eq(tag, "header") || eq(tag, "hgroup") || eq(tag, "hr") ||
           eq(tag, "main") || eq(tag, "menu") || eq(tag, "nav") ||
           eq(tag, "ol") || eq(tag, "p") || eq(tag, "pre") ||
           eq(tag, "search") || eq(tag, "section") || eq(tag, "table") ||
           eq(tag, "ul");
}

static int closes_on_open(const char *active, const char *incoming)
{
    if (eq(active, "option"))
        return eq(incoming, "option") || eq(incoming, "optgroup");
    if (eq(active, "optgroup")) return eq(incoming, "optgroup");
    if (eq(active, "li")) return eq(incoming, "li");
    if (eq(active, "dt") || eq(active, "dd"))
        return eq(incoming, "dt") || eq(incoming, "dd");
    if (eq(active, "p")) return opens_block_that_closes_p(incoming);
    if (eq(active, "rt") || eq(active, "rp"))
        return eq(incoming, "rt") || eq(incoming, "rp");
    if (eq(active, "td") || eq(active, "th"))
        return eq(incoming, "td") || eq(incoming, "th") ||
               eq(incoming, "tr") || eq(incoming, "tbody") ||
               eq(incoming, "thead") || eq(incoming, "tfoot");
    if (eq(active, "tr"))
        return eq(incoming, "tr") || eq(incoming, "tbody") ||
               eq(incoming, "thead") || eq(incoming, "tfoot");
    if (eq(active, "tbody") || eq(active, "thead") || eq(active, "tfoot"))
        return eq(incoming, "tbody") || eq(incoming, "thead") ||
               eq(incoming, "tfoot");
    return 0;
}

static int has_open_ancestor(int stack[], int depth, const char *tag)
{
    int i;
    for (i = depth; i >= 1; i--)
        if (eq(nodes[stack[i]].tag, tag)) return 1;
    return 0;
}

static char *read_file(const char *name)
{
    FILE *f;
    long len;
    size_t got;
    char *s;
    f = fopen(name, "rb");
    if (!f) { perror(name); exit(1); }
    if (fseek(f, 0, SEEK_END) != 0) fatal("cannot seek input");
    len = ftell(f);
    if (len < 0 || len > MAX_SOURCE) fatal("input exceeds 16 MiB limit");
    if (fseek(f, 0, SEEK_SET) != 0) fatal("cannot rewind input");
    s = (char *)xalloc((size_t)len + 1);
    got = fread(s, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) fatal("cannot read input");
    s[got] = '\0';
    return s;
}

static void parse_html(char *src)
{
    const char *p = src, *start, *vstart, *close;
    char *tag, *name, *value;
    int root, stack[MAX_DEPTH], depth = 0, line = 1, tag_line, n, k;
    int closing, selfclose, quote;
    root = add_node("#root", -1, line);
    stack[0] = root;
    while (*p) {
        if (*p != '<') {
            start = p;
            while (*p && *p != '<') p++;
            if (p > start) {
                char *raw = dup_range(start, (size_t)(p - start));
                char *decoded = html_decode(raw);
                free(raw);
                if (!whitespace_only(decoded)) {
                    n = add_node("#text", stack[depth], line);
                    nodes[n].text = decoded;
                } else free(decoded);
            }
            count_lines(start, p, &line);
            continue;
        }
        tag_line = line;
        if (strncmp(p, "<!--", 4) == 0) {
            close = strstr(p + 4, "-->");
            if (!close) fatal("unclosed HTML comment");
            close += 3;
            count_lines(p, close, &line);
            p = close;
            continue;
        }
        if (p[1] == '!' || p[1] == '?') {
            close = strchr(p, '>');
            if (!close) fatal("unclosed <! ... > tag");
            count_lines(p, close + 1, &line);
            p = close + 1;
            continue;
        }
        start = p++;
        closing = 0;
        if (*p == '/') { closing = 1; p++; }
        while (*p && isspace((unsigned char)*p)) p++;
        vstart = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == ':')) p++;
        if (p == vstart) fatal("invalid HTML tag");
        tag = dup_range(vstart, (size_t)(p - vstart));
        for (k = 0; tag[k]; k++) tag[k] = (char)tolower((unsigned char)tag[k]);
        if (closing) {
            close = strchr(p, '>');
            if (!close) fatal("unclosed closing tag");
            /* Optional child end tags are implied by the closing parent. */
            if (has_open_ancestor(stack, depth, tag)) {
                while (depth > 0 && !eq(nodes[stack[depth]].tag, tag) &&
                       optional_end_tag(nodes[stack[depth]].tag)) depth--;
            }
            if (depth <= 0 || !eq(nodes[stack[depth]].tag, tag)) {
                fprintf(stderr, "%s:%d: mismatched closing tag </%s> (inside <%s>)\n",
                        source_name, tag_line, tag,
                        nodes[stack[depth]].tag);
                exit(1);
            }
            depth--;
            count_lines(start, close + 1, &line);
            p = close + 1;
            free(tag);
            continue;
        }
        /* A new option/li/table cell, etc. can implicitly end its sibling. */
        while (depth > 0 && closes_on_open(nodes[stack[depth]].tag, tag))
            depth--;
        n = add_node(tag, stack[depth], tag_line);
        free(tag);
        selfclose = 0;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '>') { p++; break; }
            if (*p == '/' && p[1] == '>') { selfclose = 1; p += 2; break; }
            if (!*p) fatal("unclosed opening tag");
            vstart = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':')) p++;
            if (p == vstart) { p++; continue; }
            name = dup_range(vstart, (size_t)(p - vstart));
            for (k = 0; name[k]; k++) name[k] = (char)tolower((unsigned char)name[k]);
            while (*p && isspace((unsigned char)*p)) p++;
            value = dup_s("");
            if (*p == '=') {
                free(value);
                p++;
                while (*p && isspace((unsigned char)*p)) p++;
                quote = 0;
                if (*p == '\'' || *p == '\"') quote = *p++;
                vstart = p;
                if (quote) {
                    while (*p && *p != quote) p++;
                    if (!*p) fatal("unclosed attribute quote");
                    value = dup_range(vstart, (size_t)(p - vstart));
                    p++;
                } else {
                    while (*p && !isspace((unsigned char)*p) && *p != '>') p++;
                    value = dup_range(vstart, (size_t)(p - vstart));
                }
            }
            add_attr(n, name, value);
            free(name);
            free(value);
        }
        count_lines(start, p, &line);
        if (eq(nodes[n].tag, "script") || eq(nodes[n].tag, "style")) {
            char endtag[32];
            sprintf(endtag, "</%s>", nodes[n].tag);
            close = strstr(p, endtag);
            if (!close) { warn_at(n, "unclosed <%s>, ignoring remainder", nodes[n].tag); break; }
            count_lines(p, close + strlen(endtag), &line);
            p = close + strlen(endtag);
            continue;
        }
        if (!selfclose && !void_tag(nodes[n].tag)) {
            depth++;
            if (depth >= MAX_DEPTH) fatal("HTML nesting too deep");
            stack[depth] = n;
        }
    }
    if (depth != 0) {
        fprintf(stderr, "%s: unclosed <%s> near line %d\n", source_name,
                nodes[stack[depth]].tag, nodes[stack[depth]].line);
        exit(1);
    }
}

static void property(const char *key, const char *val, int indent)
{
    int i;
    for (i = 0; i < indent; i++) fputs("  ", xmlout);
    fprintf(xmlout, "<property name=\"%s\">", key);
    xml_escape(xmlout, val);
    fputs("</property>\n", xmlout);
}

static void property_num(const char *key, int val, int indent)
{
    char tmp[32];
    sprintf(tmp, "%d", val);
    property(key, tmp, indent);
}

static void pad(int indent)
{
    int i;
    for (i = 0; i < indent; i++) fputs("  ", xmlout);
}

static void get_id(int n, char *out, size_t cap)
{
    const char *raw = attr(n, "id");
    size_t k = 0;
    int i;
    if (!*raw) { sprintf(out, "node_%d", n); return; }
    if (isdigit((unsigned char)*raw)) out[k++] = '_';
    while (*raw && k + 1 < cap) {
        char ch = *raw++;
        if (isalnum((unsigned char)ch) || ch == '_' || ch == '-') out[k++] = ch;
        else out[k++] = '_';
    }
    out[k] = '\0';
    for (i = 0; i < n; i++) {
        if (eq(attr(i, "id"), attr(n, "id"))) {
            warn_at(n, "duplicate HTML id '%s'", attr(n, "id"));
            sprintf(out, "node_%d", n);
            return;
        }
    }
}

static void append_buf(char *dst, size_t cap, const char *src)
{
    size_t a = strlen(dst), b = strlen(src);
    if (a + b + 1 >= cap) fatal("flattened text exceeds MAX_TEXT");
    memcpy(dst + a, src, b + 1);
}

static void flatten_text(int n, char *dest, size_t cap)
{
    int c;
    if (eq(nodes[n].tag, "#text")) {
        append_buf(dest, cap, nodes[n].text);
        return;
    }
    if (eq(nodes[n].tag, "br")) { append_buf(dest, cap, "\n"); return; }
    for (c = nodes[n].first; c >= 0; c = nodes[c].next)
        flatten_text(c, dest, cap);
}

/* The following color table is the Tailwind v3 palette at 50-950. */
typedef struct Palette {
    const char *name;
    const char *values[11];
} Palette;
static const char *shades[11] =
    { "50", "100", "200", "300", "400", "500", "600", "700", "800", "900", "950" };
static const Palette palettes[] = {
    {"slate", {"#f8fafc","#f1f5f9","#e2e8f0","#cbd5e1","#94a3b8","#64748b","#475569","#334155","#1e293b","#0f172a","#020617"}},
    {"gray", {"#f9fafb","#f3f4f6","#e5e7eb","#d1d5db","#9ca3af","#6b7280","#4b5563","#374151","#1f2937","#111827","#030712"}},
    {"zinc", {"#fafafa","#f4f4f5","#e4e4e7","#d4d4d8","#a1a1aa","#71717a","#52525b","#3f3f46","#27272a","#18181b","#09090b"}},
    {"neutral", {"#fafafa","#f5f5f5","#e5e5e5","#d4d4d4","#a3a3a3","#737373","#525252","#404040","#262626","#171717","#0a0a0a"}},
    {"stone", {"#fafaf9","#f5f5f4","#e7e5e4","#d6d3d1","#a8a29e","#78716c","#57534e","#44403c","#292524","#1c1917","#0c0a09"}},
    {"blue", {"#eff6ff","#dbeafe","#bfdbfe","#93c5fd","#60a5fa","#3b82f6","#2563eb","#1d4ed8","#1e40af","#1e3a8a","#172554"}},
    {"sky", {"#f0f9ff","#e0f2fe","#bae6fd","#7dd3fc","#38bdf8","#0ea5e9","#0284c7","#0369a1","#075985","#0c4a6e","#082f49"}},
    {"indigo", {"#eef2ff","#e0e7ff","#c7d2fe","#a5b4fc","#818cf8","#6366f1","#4f46e5","#4338ca","#3730a3","#312e81","#1e1b4b"}},
    {"emerald", {"#ecfdf5","#d1fae5","#a7f3d0","#6ee7b7","#34d399","#10b981","#059669","#047857","#065f46","#064e3b","#022c22"}},
    {"green", {"#f0fdf4","#dcfce7","#bbf7d0","#86efac","#4ade80","#22c55e","#16a34a","#15803d","#166534","#14532d","#052e16"}},
    {"red", {"#fef2f2","#fee2e2","#fecaca","#fca5a5","#f87171","#ef4444","#dc2626","#b91c1c","#991b1b","#7f1d1d","#450a0a"}},
    {"rose", {"#fff1f2","#ffe4e6","#fecdd3","#fda4af","#fb7185","#f43f5e","#e11d48","#be123c","#9f1239","#881337","#4c0519"}},
    {"yellow", {"#fefce8","#fef9c3","#fef08a","#fde047","#facc15","#eab308","#ca8a04","#a16207","#854d0e","#713f12","#422006"}},
    {"amber", {"#fffbeb","#fef3c7","#fde68a","#fcd34d","#fbbf24","#f59e0b","#d97706","#b45309","#92400e","#78350f","#451a03"}},
    {"orange", {"#fff7ed","#ffedd5","#fed7aa","#fdba74","#fb923c","#f97316","#ea580c","#c2410c","#9a3412","#7c2d12","#431407"}},
    {"purple", {"#faf5ff","#f3e8ff","#e9d5ff","#d8b4fe","#c084fc","#a855f7","#9333ea","#7e22ce","#6b21a8","#581c87","#3b0764"}},
    {"cyan", {"#ecfeff","#cffafe","#a5f3fc","#67e8f9","#22d3ee","#06b6d4","#0891b2","#0e7490","#155e75","#164e63","#083344"}},
    {"teal", {"#f0fdfa","#ccfbf1","#99f6e4","#5eead4","#2dd4bf","#14b8a6","#0d9488","#0f766e","#115e59","#134e4a","#042f2e"}}
};

static const char *tw_color(const char *s)
{
    size_t i, j;
    const char *hyphen;
    if (eq(s, "white")) return "#ffffff";
    if (eq(s, "black")) return "#000000";
    if (eq(s, "transparent")) return "transparent";
    if (eq(s, "current")) return "currentColor";
    hyphen = strrchr(s, '-');
    if (!hyphen) return NULL;
    for (i = 0; i < sizeof(palettes)/sizeof(palettes[0]); i++) {
        if ((size_t)(hyphen - s) == strlen(palettes[i].name) &&
            strncmp(s, palettes[i].name, (size_t)(hyphen - s)) == 0) {
            for (j = 0; j < 11; j++)
                if (eq(hyphen + 1, shades[j])) return palettes[i].values[j];
        }
    }
    return NULL;
}

/* alpha() is supported in GTK CSS, and handles Tailwind /NN transparency. */
static int tw_color_css(const char *raw, char *dst, size_t cap)
{
    const char *slash = strrchr(raw, '/'), *col;
    char name[80];
    char *end;
    long amount;
    if (!slash) {
        col = tw_color(raw);
        if (!col) return 0;
        if (strlen(col) + 1 > cap) return 0;
        strcpy(dst, col);
        return 1;
    }
    if ((size_t)(slash - raw) >= sizeof(name)) return 0;
    memcpy(name, raw, (size_t)(slash - raw));
    name[slash - raw] = '\0';
    col = tw_color(name);
    if (!col || !slash[1]) return 0;
    amount = strtol(slash + 1, &end, 10);
    if (*end || amount < 0 || amount > 100) return 0;
    if (cap < 100) return 0;
    sprintf(dst, "alpha(%s, %.2f)", col, (double)amount / 100.0);
    return 1;
}

static int emit_border_side(const char *side, const char *suffix)
{
    int width = 1;
    if (*suffix) {
        if (eq(suffix, "-0")) width = 0;
        else if (eq(suffix, "-2")) width = 2;
        else if (eq(suffix, "-4")) width = 4;
        else return 0;
    }
    fprintf(cssout, "  border-%s-style: solid; border-%s-width: %dpx;\n",
            side, side, width);
    return 1;
}

static void css_size(const char *prop, int value)
{
    fprintf(cssout, "  %s: %dpx;\n", prop, value);
}

/* Emits only CSS GTK understands; layout handled separately via Builder. */
static int utility_css(const char *t)
{
    const char *base = NULL;
    int n, nums;
    char color[128];
    if (begins(t, "bg-") && tw_color_css(t + 3, color, sizeof(color))) {
        fprintf(cssout, "  background-color: %s;\n", color); return 1;
    }
    if (begins(t, "text-") && tw_color_css(t + 5, color, sizeof(color))) {
        fprintf(cssout, "  color: %s;\n", color); return 1;
    }
    if (begins(t, "border-") && tw_color_css(t + 7, color, sizeof(color))) {
        fprintf(cssout, "  border-color: %s;\n", color); return 1;
    }
    if (begins(t, "shadow-") && tw_color_css(t + 7, color, sizeof(color))) {
        fprintf(cssout, "  box-shadow: 0 4px 8px %s;\n", color); return 1;
    }
    if (begins(t, "p-") || begins(t, "m-")) {
        base = t[0] == 'p' ? "padding" : "margin";
        nums = tw_units(t + 2);
        if (nums >= 0) { css_size(base, nums); return 1; }
    }
    if (eq(t, "mx-auto")) return 1; /* handled as GtkWidget:halign */
    if ((begins(t, "px-") || begins(t, "py-") ||
         begins(t, "pt-") || begins(t, "pb-") ||
         begins(t, "pl-") || begins(t, "pr-") ||
         begins(t, "mx-") || begins(t, "my-") ||
         begins(t, "mt-") || begins(t, "mb-") ||
         begins(t, "ml-") || begins(t, "mr-"))) {
        base = t[0] == 'p' ? "padding" : "margin";
        nums = tw_units(t + 3);
        if (nums < 0) return 0;
        if (t[1] == 'x') {
            fprintf(cssout, "  %s-left: %dpx; %s-right: %dpx;\n", base, nums, base, nums);
        } else if (t[1] == 'y') {
            fprintf(cssout, "  %s-top: %dpx; %s-bottom: %dpx;\n", base, nums, base, nums);
        } else {
            const char *side = t[1] == 't' ? "top" : t[1] == 'b' ? "bottom" :
                               t[1] == 'l' ? "left" : "right";
            fprintf(cssout, "  %s-%s: %dpx;\n", base, side, nums);
        }
        return 1;
    }
    if (eq(t, "text-xs")) { css_size("font-size", 12); return 1; }
    if (eq(t, "text-sm")) { css_size("font-size", 14); return 1; }
    if (eq(t, "text-base")) { css_size("font-size", 16); return 1; }
    if (eq(t, "text-lg")) { css_size("font-size", 18); return 1; }
    if (eq(t, "text-xl")) { css_size("font-size", 20); return 1; }
    if (eq(t, "text-2xl")) { css_size("font-size", 24); return 1; }
    if (eq(t, "text-3xl")) { css_size("font-size", 30); return 1; }
    if (eq(t, "text-4xl")) { css_size("font-size", 36); return 1; }
    if (eq(t, "font-normal")) { fputs("  font-weight: 400;\n", cssout); return 1; }
    if (eq(t, "font-medium")) { fputs("  font-weight: 500;\n", cssout); return 1; }
    if (eq(t, "font-semibold")) { fputs("  font-weight: 600;\n", cssout); return 1; }
    if (eq(t, "font-bold")) { fputs("  font-weight: 700;\n", cssout); return 1; }
    if (eq(t, "font-extrabold")) { fputs("  font-weight: 800;\n", cssout); return 1; }
    if (eq(t, "font-black")) { fputs("  font-weight: 900;\n", cssout); return 1; }
    if (eq(t, "font-mono")) { fputs("  font-family: monospace;\n", cssout); return 1; }
    if (eq(t, "font-sans")) { fputs("  font-family: sans-serif;\n", cssout); return 1; }
    if (eq(t, "italic")) { fputs("  font-style: italic;\n", cssout); return 1; }
    if (eq(t, "underline")) { fputs("  text-decoration-line: underline;\n", cssout); return 1; }
    if (eq(t, "line-through")) { fputs("  text-decoration-line: line-through;\n", cssout); return 1; }
    if (eq(t, "uppercase")) { fputs("  text-transform: uppercase;\n", cssout); return 1; }
    if (eq(t, "lowercase")) { fputs("  text-transform: lowercase;\n", cssout); return 1; }
    if (eq(t, "tracking-tight")) { fputs("  letter-spacing: -0.025em;\n", cssout); return 1; }
    if (eq(t, "tracking-widest")) { fputs("  letter-spacing: 0.1em;\n", cssout); return 1; }
    if (eq(t, "tracking-wider")) { fputs("  letter-spacing: 0.05em;\n", cssout); return 1; }
    if (eq(t, "tracking-wide")) { fputs("  letter-spacing: 0.025em;\n", cssout); return 1; }
    if (eq(t, "tracking-normal")) { fputs("  letter-spacing: 0;\n", cssout); return 1; }
    if (begins(t, "leading-")) {
        nums = tw_units(t + 8);
        if (nums >= 0) { fprintf(cssout, "  line-height: %dpx;\n", nums); return 1; }
    }
    if (eq(t, "antialiased")) return 1; /* hint not available per-widget in GTK */
    if (eq(t, "rounded-none")) { css_size("border-radius", 0); return 1; }
    if (eq(t, "rounded-sm")) { css_size("border-radius", 2); return 1; }
    if (eq(t, "rounded") || eq(t, "rounded-md")) { css_size("border-radius", 6); return 1; }
    if (eq(t, "rounded-lg")) { css_size("border-radius", 8); return 1; }
    if (eq(t, "rounded-xl")) { css_size("border-radius", 12); return 1; }
    if (eq(t, "rounded-2xl")) { css_size("border-radius", 16); return 1; }
    if (eq(t, "rounded-full")) { css_size("border-radius", 9999); return 1; }
    if (eq(t, "rounded-t-md")) {
        fputs("  border-top-left-radius: 6px; border-top-right-radius: 6px;\n", cssout);
        return 1;
    }
    if (eq(t, "border")) {
        fputs("  border-style: solid; border-width: 1px; border-color: #374151;\n", cssout);
        return 1;
    }
    if (eq(t, "border-0") || eq(t, "border-2") || eq(t, "border-4")) {
        n = t[7] - '0'; css_size("border-width", n);
        fputs("  border-style: solid;\n", cssout); return 1;
    }
    if (begins(t, "border-t")) return emit_border_side("top", t + 8);
    if (begins(t, "border-b")) return emit_border_side("bottom", t + 8);
    if (begins(t, "border-l")) return emit_border_side("left", t + 8);
    if (begins(t, "border-r")) return emit_border_side("right", t + 8);
    if (eq(t, "shadow") || eq(t, "shadow-sm")) {
        fputs("  box-shadow: 0 1px 3px alpha(black, 0.20);\n", cssout);
        return 1;
    }
    if (eq(t, "shadow-md") || eq(t, "shadow-lg")) {
        fputs("  box-shadow: 0 4px 8px alpha(black, 0.25);\n", cssout);
        return 1;
    }
    if (begins(t, "w-") || begins(t, "h-")) {
        if (eq(t, "w-full") || eq(t, "h-full") || eq(t, "w-auto") || eq(t, "h-auto")) return 1;
        nums = pixel_value(t + 2);
        if (nums >= 0) { css_size(t[0] == 'w' ? "min-width" : "min-height", nums); return 1; }
    }
    if (begins(t, "max-w-") || begins(t, "max-h-")) return 1;
    if (begins(t, "min-w-") || begins(t, "min-h-")) {
        if (eq(t, "min-h-screen")) return 1;
        nums = pixel_value(t + 6);
        if (nums >= 0) { css_size(t[4] == 'w' ? "min-width" : "min-height", nums); return 1; }
    }
    if (eq(t, "opacity-50")) { fputs("  opacity: 0.5;\n", cssout); return 1; }
    if (eq(t, "opacity-75")) { fputs("  opacity: 0.75;\n", cssout); return 1; }
    if (eq(t, "outline-none")) { fputs("  outline-style: none;\n", cssout); return 1; }
    if (eq(t, "cursor-pointer") || eq(t, "block") || eq(t, "inline-block") ||
        eq(t, "inline") || eq(t, "flex") || eq(t, "grid") ||
        eq(t, "flex-col") || eq(t, "flex-row") || eq(t, "items-center") ||
        eq(t, "items-start") || eq(t, "items-end") ||
        eq(t, "justify-center") || eq(t, "justify-between") ||
        eq(t, "justify-start") || eq(t, "justify-end") ||
        eq(t, "flex-1") || eq(t, "grow") || eq(t, "shrink-0") ||
        eq(t, "w-full") || eq(t, "h-full") ||
        eq(t, "text-center") || eq(t, "text-left") || eq(t, "text-right") ||
        eq(t, "hidden") || eq(t, "truncate") || eq(t, "whitespace-nowrap") ||
        eq(t, "overflow-hidden") || eq(t, "overflow-x-auto") ||
        eq(t, "flex-wrap") || eq(t, "items-stretch") ||
        eq(t, "space-y-0") || eq(t, "border-collapse") || eq(t, "min-h-screen") ||
        eq(t, "min-w-0") || eq(t, "divide-y")) return 1;
    if (begins(t, "divide-") && tw_color_css(t + 7, color, sizeof(color))) return 1;
    if (begins(t, "gap-") || begins(t, "gap-x-") || begins(t, "gap-y-") ||
        begins(t, "space-y-") || begins(t, "grid-cols-") ||
        begins(t, "col-span-")) return 1;
    return 0;
}

/* Output active breakpoints in base->sm->md->lg->xl->2xl order.
 * :hover/:focus are live GTK CSS pseudo states, not compile-time snapshots.
 */
static void emit_dividers(int n);

/* Reuse one ANSI-C tmpfile as a property staging stream. Do not create
 * thousands of empty GTK CSS selectors, or repeat rules at each breakpoint.
 * All active breakpoint declarations are combined in the same rule in order.
 */
static void emit_css_state(int n, int state_index)
{
    const char *classes = attr(n, "class"), *p, *start, *utility, *state;
    FILE *actual = cssout;
    char buffer[4096];
    char *word;
    int level, bp;
    long size;
    size_t take, got;
    if (fseek(css_stage, 0L, SEEK_SET)) fatal("CSS staging seek failed");
    cssout = css_stage;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = classes;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (p == start) continue;
            word = dup_range(start, (size_t)(p - start));
            if (split_utility(word, &bp, &state, &utility) &&
                bp == breakpoints[level] &&
                ((state_index == 0 && !*state) ||
                 (state_index == 1 && eq(state, "hover")) ||
                 (state_index == 2 && eq(state, "focus")) ||
                 (state_index == 3 && eq(state, "placeholder")))) {
                utility_css(utility);
            }
            free(word);
        }
    }
    if (fflush(css_stage)) fatal("CSS staging write failed");
    size = ftell(css_stage);
    cssout = actual;
    if (size < 0) fatal("CSS staging size failed");
    if (!size) return;
    style_has_css[n] = 1;
    fprintf(cssout, ".tw-%d", n);
    if (state_index == 1) fputs(":hover", cssout);
    else if (state_index == 2) fputs(":focus", cssout);
    else if (state_index == 3) fputs(" text placeholder", cssout);
    fputs(" {\n", cssout);
    if (fseek(css_stage, 0L, SEEK_SET)) fatal("CSS staging rewind failed");
    while (size > 0) {
        take = (size > (long)sizeof(buffer)) ? sizeof(buffer) : (size_t)size;
        got = fread(buffer, 1, take, css_stage);
        if (got != take || fwrite(buffer, 1, got, cssout) != got)
            fatal("CSS staging copy failed");
        size -= (long)got;
    }
    fputs("}\n", cssout);
}

static void emit_css(int n)
{
    const char *classes = attr(n, "class"), *p, *start, *utility, *state;
    FILE *actual = cssout;
    char *word;
    int bp, valid, i;
    if (!*classes) return;
    /* Validate exactly once per distinct class set, including inactive
     * responsive utilities, without adding empty selectors. */
    p = classes;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p == start) continue;
        word = dup_range(start, (size_t)(p - start));
        valid = split_utility(word, &bp, &state, &utility);
        if (!valid) warn_at(n, "unrecognized variant '%s'", word);
        else {
            if (fseek(css_stage, 0L, SEEK_SET)) fatal("CSS staging seek failed");
            cssout = css_stage;
            if (!utility_css(utility)) warn_at(n, "unsupported Tailwind utility '%s'", word);
            cssout = actual;
        }
        free(word);
    }
    for (i = 0; i < 4; i++) emit_css_state(n, i);
    emit_dividers(n);
}

/* Iterate base utilities first, then active breakpoints from smaller to larger.
 * Within one breakpoint, later tokens override earlier ones.
 */
static void emit_dividers(int n)
{
    const char *p, *start, *utility, *state;
    char *word;
    int level, bp;
    char color[128];
    strcpy(color, "#374151");
    if (!class_active(n, "divide-y")) return;
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
                bp == breakpoints[level] && !*state &&
                begins(utility, "divide-") && !eq(utility, "divide-y")) {
                char candidate[128];
                if (tw_color_css(utility + 7, candidate, sizeof(candidate)))
                    strcpy(color, candidate);
            }
            free(word);
        }
    }
    fprintf(cssout, ".tw-divider-%d { border-top: 1px solid %s; }\n", n, color);
}

static int class_number(int n, const char *prefix, int def)
{
    const char *p, *start, *utility, *state;
    int level, result = def, v;
    char *word;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (p == start) continue;
            word = dup_range(start, (size_t)(p - start));
            if (word_level(word, breakpoints[level], &state, &utility) &&
                !*state && begins(utility, prefix)) {
                v = pixel_value(utility + strlen(prefix));
                if (v >= 0) result = v;
            }
            free(word);
        }
    }
    return result;
}

static int active_number(int n, const char *prefix, int def, int lo, int hi)
{
    const char *p, *start, *utility, *state;
    int level, result = def, v;
    char *word;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (p == start) continue;
            word = dup_range(start, (size_t)(p - start));
            if (word_level(word, breakpoints[level], &state, &utility) &&
                !*state && begins(utility, prefix)) {
                v = parse_int(utility + strlen(prefix), result);
                if (v >= lo && v <= hi) result = v;
            }
            free(word);
        }
    }
    return result;
}

/* Resolve Tailwind display variants in order: hidden lg:block must be
 * visible when the compile-time viewport satisfies the lg breakpoint. */
static int resolved_hidden(int n)
{
    const char *p, *start, *state, *utility;
    int level, hidden = 0;
    char *word;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (start == p) continue;
            word = dup_range(start, (size_t)(p - start));
            if (word_level(word, breakpoints[level], &state, &utility) && !*state) {
                if (eq(utility, "hidden")) hidden = 1;
                else if (eq(utility, "block") || eq(utility, "inline") ||
                         eq(utility, "inline-block") || eq(utility, "flex") ||
                         eq(utility, "inline-flex") || eq(utility, "grid") ||
                         eq(utility, "inline-grid")) hidden = 0;
            }
            free(word);
        }
    }
    return hidden;
}

/* Support the common Tailwind arbitrary grid form
 * grid-cols-[300px_minmax(0,1fr)] without treating it as 1 column.
 * The first fixed pixel column becomes the GTK sidebar width. */
static void grid_template(int n, int *columns, int *first_pixels)
{
    const char *p, *start, *state, *utility, *q;
    int level, v, count_columns, depth;
    char *word, *end;
    long fixed;
    *columns = 1;
    *first_pixels = 0;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (start == p) continue;
            word = dup_range(start, (size_t)(p - start));
            if (word_level(word, breakpoints[level], &state, &utility) &&
                !*state && begins(utility, "grid-cols-")) {
                q = utility + 10;
                if (*q == '[') {
                    count_columns = 1;
                    depth = 0;
                    for (end = (char *)q + 1; *end && *end != ']'; end++) {
                        if (*end == '(') depth++;
                        else if (*end == ')' && depth > 0) depth--;
                        else if (*end == '_' && depth == 0) count_columns++;
                    }
                    if (*end == ']' && end[1] == '\0' &&
                        count_columns > 0 && count_columns <= 12) {
                        *columns = count_columns;
                        *first_pixels = 0;
                        fixed = strtol(q + 1, &end, 10);
                        if (fixed > 0 && fixed < 10000 &&
                            strncmp(end, "px_", 3) == 0)
                            *first_pixels = (int)fixed;
                    }
                } else {
                    v = parse_int(q, -1);
                    if (v >= 1 && v <= 12) {
                        *columns = v;
                        *first_pixels = 0;
                    }
                }
            }
            free(word);
        }
    }
}

static int grid_cols(int n)
{
    int cols, fixed;
    grid_template(n, &cols, &fixed);
    return cols;
}
static int grid_fixed_first(int n)
{
    int cols, fixed;
    grid_template(n, &cols, &fixed);
    return fixed;
}
static int col_span(int n) { return active_number(n, "col-span-", 1, 1, 12); }

static int is_grid(int n)
{
    return class_active(n, "grid") || eq(nodes[n].tag, "table");
}

static int is_vertical(int n)
{
    const char *p, *start, *utility, *state;
    char *word;
    int level, result = 1;
    for (level = 0; level < 6; level++) {
        if (breakpoints[level] > viewport_width) break;
        p = attr(n, "class");
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (p == start) continue;
            word = dup_range(start, (size_t)(p - start));
            if (word_level(word, breakpoints[level], &state, &utility) && !*state) {
                if (eq(utility, "flex") || eq(utility, "flex-row")) result = 0;
                else if (eq(utility, "flex-col")) result = 1;
            }
            free(word);
        }
    }
    return result;
}

static int is_label(int n)
{
    const char *t = nodes[n].tag;
    return eq(t, "label") || eq(t, "span") || eq(t, "p") ||
        eq(t, "h1") || eq(t, "h2") || eq(t, "h3") ||
        eq(t, "h4") || eq(t, "h5") || eq(t, "h6") ||
        eq(t, "small") || eq(t, "strong") || eq(t, "b") ||
        eq(t, "em") || eq(t, "i") || eq(t, "code") ||
        eq(t, "pre") || eq(t, "li") || eq(t, "td") || eq(t, "th") ||
        eq(t, "caption") || eq(t, "dt") || eq(t, "dd") ||
        eq(t, "#text") || eq(t, "output");
}

static void emit_node(int n, int indent, int grid_col, int grid_row, int grid_span);

static void emit_spacer(int indent, int vertical)
{
    pad(indent); fputs("<child><object class=\"GtkBox\">\n", xmlout);
    property(vertical ? "vexpand" : "hexpand", "true", indent + 1);
    property_num("spacing", 0, indent + 1);
    pad(indent); fputs("</object></child>\n", xmlout);
}

static void emit_children(int n, int indent)
{
    int c, vertical = is_vertical(n), first = 1;
    int justify_between = class_active(n, "justify-between");
    int justify_center = class_active(n, "justify-center");
    int justify_end = class_active(n, "justify-end");
    if (justify_center || justify_end) emit_spacer(indent, vertical);
    for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
        if (!first && justify_between) emit_spacer(indent, vertical);
        emit_node(c, indent, -1, -1, 1);
        first = 0;
    }
    if (justify_center) emit_spacer(indent, vertical);
}

static void emit_styles(int n, int indent)
{
    const char *t = nodes[n].tag;
    int h = 0, needed, ancestor, row, group;
    if (eq(t, "h1")) h = 1;
    else if (eq(t, "h2")) h = 2;
    else if (eq(t, "h3")) h = 3;
    else if (eq(t, "th")) h = 4;
    needed = h || (style_owner[n] && style_has_css[style_owner[n]]);
    if (!needed && eq(t, "td")) {
        row = nodes[n].parent;
        for (ancestor = row; ancestor > 0; ancestor = nodes[ancestor].parent) {
            if ((eq(nodes[ancestor].tag, "tr") || eq(nodes[ancestor].tag, "thead") ||
                 eq(nodes[ancestor].tag, "tbody") || eq(nodes[ancestor].tag, "tfoot")) &&
                style_owner[ancestor] && style_has_css[style_owner[ancestor]]) needed = 1;
            if (eq(nodes[ancestor].tag, "table")) break;
        }
        group = row >= 0 ? nodes[row].parent : -1;
        if (row >= 0 && group >= 0 && eq(nodes[group].tag, "tbody") &&
            class_active(group, "divide-y") && nodes[group].first != row) needed = 1;
    }
    if (needed) {
        pad(indent);
        fputs("<style>", xmlout);
        if (h == 1) fputs("<class name=\"html-h1\"/>", xmlout);
        if (h == 2) fputs("<class name=\"html-h2\"/>", xmlout);
        if (h == 3) fputs("<class name=\"html-h3\"/>", xmlout);
        if (h == 4) fputs("<class name=\"html-th\"/>", xmlout);
        if (style_owner[n] && style_has_css[style_owner[n]])
            fprintf(xmlout, "<class name=\"tw-%d\"/>", style_owner[n]);
        if (eq(t, "td") || eq(t, "th")) {
            row = nodes[n].parent;
            /* GtkGrid doesn't create <tr>/<tbody> widgets, so propagate
             * their generated styles onto the native table cells. */
            for (ancestor = row; ancestor > 0; ancestor = nodes[ancestor].parent) {
                if ((eq(nodes[ancestor].tag, "tr") || eq(nodes[ancestor].tag, "thead") ||
                     eq(nodes[ancestor].tag, "tbody") || eq(nodes[ancestor].tag, "tfoot")) &&
                    style_owner[ancestor] && style_has_css[style_owner[ancestor]])
                    fprintf(xmlout, "<class name=\"tw-%d\"/>", style_owner[ancestor]);
                if (eq(nodes[ancestor].tag, "table")) break;
            }
            group = row >= 0 ? nodes[row].parent : -1;
            if (row >= 0 && group >= 0 && eq(nodes[group].tag, "tbody") &&
                class_active(group, "divide-y") && nodes[group].first != row)
                fprintf(xmlout, "<class name=\"tw-divider-%d\"/>", style_owner[group]);
        }
        fputs("</style>\n", xmlout);
    }
}

static void emit_common(int n, int indent, const char *kind, int grid_col, int inside_cap)
{
    int parent = nodes[n].parent, vertical, v;
    int expand_x = 0, expand_y = 0;
    if (resolved_hidden(n)) property("visible", "false", indent);
    if (class_active(n, "w-full") || eq(nodes[n].tag, "body")) expand_x = 1;
    if (class_active(n, "h-full") || class_active(n, "min-h-screen") ||
        eq(nodes[n].tag, "body")) expand_y = 1;
    if (parent >= 0) {
        vertical = is_vertical(parent);
        if (class_active(n, "flex-1") || class_active(n, "grow")) {
            if (vertical) expand_y = 1;
            else expand_x = 1;
        }
        if (!inside_cap && class_active(parent, "items-center"))
            property(vertical ? "halign" : "valign", "center", indent);
        if (!inside_cap && class_active(parent, "items-end"))
            property(vertical ? "halign" : "valign", "end", indent);
        if (!inside_cap && class_active(parent, "items-start"))
            property(vertical ? "halign" : "valign", "start", indent);
    }
    if (parent >= 0 && grid_col > 0 && grid_fixed_first(parent) > 0)
        expand_x = 1;
    if (expand_x) property("hexpand", "true", indent);
    if (expand_y) property("vexpand", "true", indent);
    if (!inside_cap && class_active(n, "mx-auto") && !eq(nodes[n].tag, "body"))
        property("halign", "center", indent);
    if (parent >= 0 && grid_col == 0 && grid_fixed_first(parent) > 0)
        property_num("width-request", grid_fixed_first(parent), indent);
    if (eq(kind, "GtkLabel")) {
        property("xalign", class_active(n, "text-center") ? "0.5" :
                          class_active(n, "text-right") ? "1.0" : "0.0", indent);
    }
    if (eq(kind, "GtkLabel") && class_active(n, "truncate"))
        property("ellipsize", "end", indent);
    v = class_number(n, "min-w-", -1);
    if (v < 0) v = class_number(n, "w-", -1);
    if (v >= 0 && !class_active(n, "w-full")) property_num("width-request", v, indent);
    v = class_number(n, "min-h-", -1);
    if (v < 0) v = class_number(n, "h-", -1);
    if (v >= 0 && !class_active(n, "h-full")) property_num("height-request", v, indent);
}

static void table_walk_rows(int n, int indent, int *row)
{
    int c, cell, col, span;
    for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
        if (eq(nodes[c].tag, "tr")) {
            col = 0;
            for (cell = nodes[c].first; cell >= 0; cell = nodes[cell].next) {
                if (eq(nodes[cell].tag, "td") || eq(nodes[cell].tag, "th")) {
                    span = parse_int(attr(cell, "colspan"), 1);
                    if (span < 1) span = 1;
                    emit_node(cell, indent, col, *row, span);
                    col += span;
                }
            }
            (*row)++;
        } else if (eq(nodes[c].tag, "thead") || eq(nodes[c].tag, "tbody") ||
                   eq(nodes[c].tag, "tfoot")) table_walk_rows(c, indent, row);
    }
}

static void emit_grid_children(int n, int indent)
{
    int c, col = 0, row = 0, cols = grid_cols(n), span;
    if (eq(nodes[n].tag, "table")) { table_walk_rows(n, indent, &row); return; }
    for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
        span = col_span(c);
        if (span > cols) span = cols;
        if (col + span > cols) { row++; col = 0; }
        emit_node(c, indent, col, row, span);
        col += span;
        if (col >= cols) { row++; col = 0; }
    }
}

static void emit_grid_layout(int indent, int col, int row, int span)
{
    if (col < 0) return;
    pad(indent); fputs("<layout>\n", xmlout);
    property_num("column", col, indent + 1);
    property_num("row", row, indent + 1);
    if (span > 1) property_num("column-span", span, indent + 1);
    pad(indent); fputs("</layout>\n", xmlout);
}

static void emit_node(int n, int indent, int grid_col, int grid_row, int grid_span)
{
    const char *t = nodes[n].tag, *kind = "GtkBox", *type;
    char idbuf[256];
    char *label;
    int c, spacing, rowgap, max_width, max_height, capped;
    int inner_indent, inner_col;
    if (eq(t, "html") || eq(t, "head") || eq(t, "title") ||
        eq(t, "meta") || eq(t, "link") || eq(t, "script") || eq(t, "style") ||
        eq(t, "option") || eq(t, "source")) {
        if (eq(t, "html")) emit_children(n, indent);
        return;
    }
    if (eq(t, "svg") || eq(t, "canvas") || eq(t, "iframe")) {
        warn_at(n, "<%s> needs a native replacement (omitted)", t);
        return;
    }
    if (eq(t, "hr")) kind = "GtkSeparator";
    else if (eq(t, "button")) kind = "GtkButton";
    else if (eq(t, "a") && (begins(attr(n, "href"), "http://") ||
                             begins(attr(n, "href"), "https://"))) kind = "GtkLinkButton";
    else if (eq(t, "a")) kind = "GtkButton";
    else if (eq(t, "input")) {
        type = attr(n, "type");
        if (eq(type, "checkbox") || eq(type, "radio")) kind = "GtkCheckButton";
        else if (eq(type, "submit") || eq(type, "button")) kind = "GtkButton";
        else kind = "GtkEntry";
    }
    else if (eq(t, "textarea")) kind = "GtkTextView";
    else if (eq(t, "select")) kind = "GtkDropDown";
    else if (eq(t, "img")) {
        kind = begins(attr(n, "src"), "http") ? "GtkLabel" : "GtkPicture";
        if (eq(kind, "GtkLabel")) warn_at(n, "remote image src unsupported; displaying alt text");
    }
    else if (eq(t, "progress")) kind = "GtkProgressBar";
    else if (class_active(n, "overflow-x-auto")) kind = "GtkScrolledWindow";
    else if (eq(t, "br")) kind = "GtkLabel";
    else if (is_label(n)) kind = "GtkLabel";
    else if (is_grid(n)) kind = "GtkGrid";
    else if (eq(t, "div") || eq(t, "main") || eq(t, "section") ||
             eq(t, "header") || eq(t, "footer") || eq(t, "nav") ||
             eq(t, "article") || eq(t, "aside") || eq(t, "body") ||
             eq(t, "form") || eq(t, "ul") || eq(t, "ol") || eq(t, "fieldset")) kind = "GtkBox";
    else { warn_at(n, "<%s> mapped to GtkBox", t); kind = "GtkBox"; }
    max_width = max_dimension(n, 'w');
    max_height = max_dimension(n, 'h');
    capped = (max_width >= 0 || max_height >= 0) &&
             !eq(kind, "GtkScrolledWindow");
    inner_indent = capped ? indent + 2 : indent;
    inner_col = capped ? -1 : grid_col;
    get_id(n, idbuf, sizeof(idbuf));
    pad(indent);
    fprintf(xmlout, "<child>\n");
    if (capped) {
        pad(indent + 1);
        fprintf(xmlout, "<object class=\"GtkBox\" id=\"h2g_clamp_%d_%d_%d_%d\">\n",
                n, max_width, max_height, class_active(n, "mx-auto") ? 1 : 0);
        property("orientation", "vertical", indent + 2);
        property("hexpand", "true", indent + 2);
        property("overflow", "hidden", indent + 2);
        if (resolved_hidden(n)) property("visible", "false", indent + 2);
        if (grid_col > 0 && nodes[n].parent >= 0 &&
            grid_fixed_first(nodes[n].parent) > 0)
            property("hexpand", "true", indent + 2);
        emit_grid_layout(indent + 2, grid_col, grid_row, grid_span);
        pad(indent + 2); fputs("<child>\n", xmlout);
    }
    pad(inner_indent + 1);
    fprintf(xmlout, "<object class=\"%s\" id=\"", kind);
    xml_escape(xmlout, idbuf);
    fputs("\">\n", xmlout);
    emit_common(n, inner_indent + 2, kind, grid_col, capped);
    if (eq(kind, "GtkLabel")) {
        label = (char *)xalloc(MAX_TEXT);
        label[0] = '\0';
        if (eq(t, "#text")) append_buf(label, MAX_TEXT, nodes[n].text);
        else if (eq(t, "img")) append_buf(label, MAX_TEXT, attr(n, "alt"));
        else if (eq(t, "br")) append_buf(label, MAX_TEXT, " ");
        else flatten_text(n, label, MAX_TEXT);
        property("label", label, inner_indent + 2);
        property("wrap", class_active(n, "whitespace-nowrap") || class_active(n, "truncate") ? "false" : "true", inner_indent + 2);
        free(label);
    } else if (eq(kind, "GtkButton") || eq(kind, "GtkLinkButton")) {
        label = (char *)xalloc(MAX_TEXT);
        label[0] = '\0';
        if (eq(t, "input")) append_buf(label, MAX_TEXT,
                                        *attr(n, "value") ? attr(n, "value") : "Submit");
        else flatten_text(n, label, MAX_TEXT);
        property("label", label, inner_indent + 2);
        if (eq(kind, "GtkLinkButton")) property("uri", attr(n, "href"), inner_indent + 2);
        free(label);
    } else if (eq(kind, "GtkEntry")) {
        if (*attr(n, "placeholder")) property("placeholder-text", attr(n, "placeholder"), inner_indent + 2);
        if (*attr(n, "value")) property("text", attr(n, "value"), inner_indent + 2);
        if (eq(attr(n, "type"), "password")) property("visibility", "false", inner_indent + 2);
        if (has_attr(n, "disabled")) property("sensitive", "false", inner_indent + 2);
    } else if (eq(kind, "GtkCheckButton")) {
        if (*attr(n, "value")) property("label", attr(n, "value"), inner_indent + 2);
        if (has_attr(n, "checked")) property("active", "true", inner_indent + 2);
    } else if (eq(kind, "GtkTextView")) {
        label = (char *)xalloc(MAX_TEXT);
        label[0] = '\0';
        flatten_text(n, label, MAX_TEXT);
        property("wrap-mode", "word-char", inner_indent + 2);
        pad(inner_indent + 2);
        fputs("<property name=\"buffer\"><object class=\"GtkTextBuffer\">\n", xmlout);
        property("text", label, inner_indent + 3);
        pad(inner_indent + 2);
        fputs("</object></property>\n", xmlout);
        free(label);
    } else if (eq(kind, "GtkDropDown")) {
        pad(inner_indent + 2);
        fputs("<property name=\"model\"><object class=\"GtkStringList\"><items>\n", xmlout);
        for (c = nodes[n].first; c >= 0; c = nodes[c].next) {
            int opt;
            if (eq(nodes[c].tag, "option")) {
                label = (char *)xalloc(MAX_TEXT);
                label[0] = '\0';
                flatten_text(c, label, MAX_TEXT);
                pad(inner_indent + 3); fputs("<item>", xmlout);
                xml_escape(xmlout, label);
                fputs("</item>\n", xmlout);
                free(label);
            } else if (eq(nodes[c].tag, "optgroup")) {
                for (opt = nodes[c].first; opt >= 0; opt = nodes[opt].next) {
                    label = (char *)xalloc(MAX_TEXT);
                    label[0] = '\0'; flatten_text(opt, label, MAX_TEXT);
                    pad(inner_indent + 3); fputs("<item>", xmlout);
                    xml_escape(xmlout, label); fputs("</item>\n", xmlout);
                    free(label);
                }
            }
        }
        pad(inner_indent + 2);
        fputs("</items></object></property>\n", xmlout);
    } else if (eq(kind, "GtkPicture")) {
        if (*attr(n, "src")) property("file", attr(n, "src"), inner_indent + 2);
        property("can-shrink", "true", inner_indent + 2);
    } else if (eq(kind, "GtkProgressBar")) {
        int max = parse_int(attr(n, "max"), 100);
        int value = parse_int(attr(n, "value"), 0);
        char tmp[32];
        if (max <= 0) max = 100;
        sprintf(tmp, "%.4f", (double)value/(double)max);
        property("fraction", tmp, inner_indent + 2);
    } else if (eq(kind, "GtkScrolledWindow")) {
        property("hscrollbar-policy", "automatic", inner_indent + 2);
        property("vscrollbar-policy", max_height >= 0 ? "automatic" : "never", inner_indent + 2);
        if (max_width >= 0) {
            property_num("max-content-width", max_width, inner_indent + 2);
            property("propagate-natural-width", "true", inner_indent + 2);
            property("halign", class_active(n, "mx-auto") ?
                     "center" : "start", inner_indent + 2);
        }
        if (max_height >= 0) {
            property_num("max-content-height", max_height, inner_indent + 2);
            property("propagate-natural-height", "true", inner_indent + 2);
            property("valign", "start", inner_indent + 2);
        }
    } else if (eq(kind, "GtkBox")) {
        property("orientation", is_vertical(n) ? "vertical" : "horizontal", inner_indent + 2);
        spacing = class_number(n, "gap-", 0);
        if (is_vertical(n)) spacing = class_number(n, "space-y-", spacing);
        if (spacing < 0) spacing = 0;
        property_num("spacing", spacing, inner_indent + 2);
    } else if (eq(kind, "GtkGrid")) {
        spacing = class_number(n, "gap-", 0);
        rowgap = class_number(n, "gap-y-", spacing);
        spacing = class_number(n, "gap-x-", spacing);
        if (rowgap < 0) rowgap = 0;
        if (spacing < 0) spacing = 0;
        property_num("column-spacing", spacing, inner_indent + 2);
        property_num("row-spacing", rowgap, inner_indent + 2);
        property("column-homogeneous", grid_fixed_first(n) > 0 ? "false" : "true", inner_indent + 2);
    }
    emit_styles(n, inner_indent + 2);
    emit_grid_layout(inner_indent + 2, inner_col, grid_row, grid_span);
    if (eq(kind, "GtkScrolledWindow")) {
        pad(inner_indent + 2); fputs("<child><object class=\"GtkBox\">\n", xmlout);
        property("orientation", is_vertical(n) ? "vertical" : "horizontal", inner_indent + 3);
        property_num("spacing", class_number(n, "gap-", 0), inner_indent + 3);
        emit_children(n, inner_indent + 3);
        pad(inner_indent + 2); fputs("</object></child>\n", xmlout);
    }
    if (eq(kind, "GtkBox")) emit_children(n, inner_indent + 2);
    if (eq(kind, "GtkGrid")) emit_grid_children(n, inner_indent + 2);
    pad(inner_indent + 1); fputs("</object>\n", xmlout);
    if (capped) {
        pad(indent + 2); fputs("</child>\n", xmlout);
        pad(indent + 1); fputs("</object>\n", xmlout);
    }
    pad(indent); fputs("</child>\n", xmlout);
}

static void emit_output(const char *out_ui, const char *out_css, const char *title)
{
    int n, c;
    xmlout = fopen(out_ui, "wb");
    if (!xmlout) { perror(out_ui); exit(1); }
    cssout = fopen(out_css, "wb");
    if (!cssout) { perror(out_css); exit(1); }
    fputs("/* Generated GTK4 CSS. Tailwind utility subset, not web CSS. */\n", cssout);
    fputs(".html-h1 { font-size: 28px; font-weight: 700; }\n", cssout);
    fputs(".html-h2 { font-size: 22px; font-weight: 600; }\n", cssout);
    fputs(".html-h3 { font-size: 18px; font-weight: 600; }\n", cssout);
    fputs(".html-th { font-weight: 700; }\n", cssout);
    css_stage = tmpfile();
    if (!css_stage) fatal("could not create CSS staging file");
    intern_styles();
    for (n = 1; n < count; n++)
        if (style_owner[n] == n) emit_css(n);
    if (fclose(css_stage)) fatal("could not close CSS staging file");
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", xmlout);
    fputs("<interface>\n  <requires lib=\"gtk\" version=\"4.0\"/>\n", xmlout);
    fputs("  <object class=\"GtkWindow\" id=\"main_window\">\n", xmlout);
    property("title", title, 2);
    property_num("default-width", viewport_width, 2);
    property_num("default-height", 750, 2);
    fputs("    <child>\n      <object class=\"GtkScrolledWindow\" id=\"scroll_root\">\n", xmlout);
    property("hexpand", "true", 4);
    property("vexpand", "true", 4);
    fputs("        <child>\n          <object class=\"GtkBox\" id=\"root_box\">\n", xmlout);
    property("orientation", "vertical", 6);
    property("hexpand", "true", 6);
    property("vexpand", "true", 6);

    for (c = nodes[0].first; c >= 0; c = nodes[c].next) emit_node(c, 6, -1, -1, 1);
    fputs("          </object>\n        </child>\n      </object>\n    </child>\n", xmlout);
    fputs("  </object>\n</interface>\n", xmlout);
    if (fclose(xmlout) != 0 || fclose(cssout) != 0) fatal("writing output failed");
}

int main(int argc, char **argv)
{
    char *src;
    if (argc < 4 || argc > 6) {
        fprintf(stderr, "usage: %s input.html output.ui output.css [window-title] [viewport-width]\n", argv[0]);
        return 2;
    }
    if (argc == 6) {
        viewport_width = parse_int(argv[5], -1);
        if (viewport_width < 320 || viewport_width > 10000)
            fatal("viewport width must be 320-10000 pixels");
    }
    source_name = argv[1];
    src = read_file(argv[1]);
    parse_html(src);
    emit_output(argv[2], argv[3], argc >= 5 ? argv[4] : "HTML to GTK Preview");
    fprintf(stderr, "html2gtk: %d nodes, %d warnings; wrote %s and %s\n",
            count - 1, warnings, argv[2], argv[3]);
    if (warnings >= 100) fprintf(stderr, "html2gtk: additional warnings suppressed\n");
    free(src);
    return 0;
}
