/*
 * html2gui standards parser adapter.
 *
 * The html2gui-owned implementation and its public interface use C89.
 * Lexbor is an external C99 dependency. It implements the WHATWG HTML
 * tokenizer/tree builder, DOM and W3C CSS Syntax parser.
 *
 * Parser errors are recoverable by design. These APIs fail only on serious
 * conditions (e.g. allocation failure). No layout or CSS cascade yet.
 */
#include "html2gui/parser.h"

#include <stdlib.h>
#include <lexbor/html/html.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/dom/interfaces/text.h>
#include <string.h>

struct h2g_html_document {
    lxb_html_document_t *native;
};

#define H2G_CSS_HASH_BUCKETS 8192

typedef struct H2GIndexedRule {
    const lxb_css_rule_style_t *rule;
    const lxb_char_t *anchor;
    size_t anchor_length;
    unsigned long sequence;
    int next;
} H2GIndexedRule;

struct h2g_css_stylesheet {
    lxb_css_parser_t *parser;
    lxb_css_stylesheet_t *native;
    lxb_selectors_t *matcher;
    H2GIndexedRule *styles;
    size_t styles_count;
    size_t styles_capacity;
    int buckets[H2G_CSS_HASH_BUCKETS];
    int fallback;
};

H2GHtmlDocument *h2g_html_parse(const char *utf8, size_t length)
{
    H2GHtmlDocument *document;
    lxb_status_t status;
    if (utf8 == NULL && length != 0) return NULL;

    document = (H2GHtmlDocument *)calloc(1, sizeof(*document));
    if (document == NULL) return NULL;

    document->native = lxb_html_document_create();
    if (document->native == NULL) {
        free(document);
        return NULL;
    }
    status = lxb_html_document_parse(document->native,
                (const lxb_char_t *)(utf8 != NULL ? utf8 : ""), length);
    if (status != LXB_STATUS_OK) {
        h2g_html_destroy(document);
        return NULL;
    }
    return document;
}

void h2g_html_destroy(H2GHtmlDocument *document)
{
    if (document == NULL) return;
    if (document->native != NULL)
        lxb_html_document_destroy(document->native);
    free(document);
}

const H2GDomNode *h2g_html_body(const H2GHtmlDocument *document)
{
    if (document == NULL || document->native == NULL ||
        document->native->body == NULL) return NULL;
    return (const H2GDomNode *)
        lxb_dom_interface_node(document->native->body);
}

const H2GDomNode *h2g_dom_first_child(const H2GDomNode *node)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    return n != NULL ? (const H2GDomNode *)n->first_child : NULL;
}

const H2GDomNode *h2g_dom_next_sibling(const H2GDomNode *node)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    return n != NULL ? (const H2GDomNode *)n->next : NULL;
}

const H2GDomNode *h2g_dom_parent(const H2GDomNode *node)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    return n != NULL ? (const H2GDomNode *)n->parent : NULL;
}

int h2g_dom_type(const H2GDomNode *node)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    return n != NULL ? (int)n->type : 0;
}

const char *h2g_dom_element_name(const H2GDomNode *node, size_t *length)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    if (length != NULL) *length = 0;
    if (n == NULL || n->type != LXB_DOM_NODE_TYPE_ELEMENT) return NULL;
    return (const char *)lxb_dom_element_qualified_name(
        lxb_dom_interface_element((lxb_dom_node_t *)n), length);
}

/* Iterative walk to avoid deep recursive stacks on adversarial markup. */
size_t h2g_html_body_node_count(const H2GHtmlDocument *document)
{
    const H2GDomNode *root = h2g_html_body(document);
    const H2GDomNode *node = root;
    size_t count = 0;
    if (node == NULL) return 0;
    for (;;) {
        const H2GDomNode *child;
        const H2GDomNode *next;
        count++;
        child = h2g_dom_first_child(node);
        if (child != NULL) {
            node = child;
            continue;
        }
        next = h2g_dom_next_sibling(node);
        while (next == NULL && node != root) {
            node = h2g_dom_parent(node);
            next = h2g_dom_next_sibling(node);
        }
        if (node == root) break;
        node = next;
    }
    return count;
}


/* Traverse only unconditional @layer wrappers. @media, @supports,
 * @container and other conditional/group rules need explicit evaluation
 * before their children can contribute to computed style.
 */
static const lxb_css_rule_list_t *h2g_layer_children(
                                                 const lxb_css_rule_t *rule)
{
    const lxb_css_rule_at_t *at;
    const lxb_css_at_rule__custom_t *custom;
    if (rule->type != LXB_CSS_RULE_AT_RULE) return NULL;
    at = lxb_css_rule_at(rule);
    if (at->type != LXB_CSS_AT_RULE__CUSTOM) return NULL;
    custom = at->u.custom;
    if (custom != NULL && custom->name.length == 5 &&
        memcmp(custom->name.data, "layer", 5) == 0)
        return custom->block;
    return NULL;
}

static size_t h2g_css_count_styles(const lxb_css_rule_t *rule, int depth)
{
    const lxb_css_rule_list_t *children;
    size_t count = 0;
    if (depth >= 32) return 0;
    while (rule != NULL) {
        if (rule->type == LXB_CSS_RULE_STYLE)
            count++;
        children = h2g_layer_children(rule);
        if (children != NULL)
            count += h2g_css_count_styles(children->first, depth + 1);
        rule = rule->next;
    }
    return count;
}

static unsigned long h2g_class_hash(const char *name, size_t length)
{
    unsigned long h = 2166136261UL;
    size_t i;
    for (i = 0; i < length; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619UL;
    }
    return h % H2G_CSS_HASH_BUCKETS;
}

/* Only index if every selector list can be constrained by one class
 * on the rightmost compound. Multiple list alternatives are fallback.
 * False positives are fine; false negatives are not.
 */
static void h2g_css_rule_anchor(H2GIndexedRule *entry)
{
    const lxb_css_selector_list_t *list;
    const lxb_css_selector_t *sel;
    list = entry->rule->selector;
    if (list == NULL || list->next != NULL) return;
    sel = list->last;
    while (sel != NULL) {
        if (sel->type == LXB_CSS_SELECTOR_TYPE_CLASS &&
            sel->name.data != NULL && sel->name.length != 0) {
            entry->anchor = sel->name.data;
            entry->anchor_length = sel->name.length;
            return;
        }
        if (sel->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE)
            return;
        sel = sel->prev;
    }
}

static int h2g_css_collect_styles(H2GCssStylesheet *sheet,
                    const lxb_css_rule_t *rule, int depth)
{
    const lxb_css_rule_list_t *children;
    H2GIndexedRule *entry;
    unsigned long bucket;
    int index;
    if (depth >= 32) return 1;
    while (rule != NULL) {
        if (rule->type == LXB_CSS_RULE_STYLE) {
            if (sheet->styles_count >= sheet->styles_capacity)
                return 0;
            index = (int)sheet->styles_count++;
            entry = &sheet->styles[index];
            entry->rule = lxb_css_rule_style(rule);
            entry->sequence = (unsigned long)index;
            entry->next = -1;
            h2g_css_rule_anchor(entry);
            if (entry->anchor != NULL) {
                bucket = h2g_class_hash((const char *)entry->anchor,
                                        entry->anchor_length);
                entry->next = sheet->buckets[bucket];
                sheet->buckets[bucket] = index;
            } else {
                entry->next = sheet->fallback;
                sheet->fallback = index;
            }
        }
        children = h2g_layer_children(rule);
        if (children != NULL &&
            !h2g_css_collect_styles(sheet, children->first, depth + 1))
            return 0;
        rule = rule->next;
    }
    return 1;
}

static int h2g_css_build_index(H2GCssStylesheet *sheet)
{
    const lxb_css_rule_list_t *root;
    size_t i;
    root = lxb_css_rule_list(sheet->native->root);
    if (root == NULL) return 0;
    sheet->styles_capacity = h2g_css_count_styles(root->first, 0);
    if (sheet->styles_capacity > 100000) return 0;
    sheet->styles = (H2GIndexedRule *)calloc(
        sheet->styles_capacity ? sheet->styles_capacity : 1,
        sizeof(H2GIndexedRule));
    if (sheet->styles == NULL) return 0;
    sheet->fallback = -1;
    for (i = 0; i < H2G_CSS_HASH_BUCKETS; i++) sheet->buckets[i] = -1;
    return h2g_css_collect_styles(sheet, root->first, 0);
}

H2GCssStylesheet *h2g_css_parse(const char *utf8, size_t length)
{
    H2GCssStylesheet *stylesheet;
    lxb_status_t status;
    if (utf8 == NULL && length != 0) return NULL;

    stylesheet = (H2GCssStylesheet *)calloc(1, sizeof(*stylesheet));
    if (stylesheet == NULL) return NULL;
    stylesheet->parser = lxb_css_parser_create();
    if (stylesheet->parser == NULL) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }
    status = lxb_css_parser_init(stylesheet->parser, NULL);
    if (status != LXB_STATUS_OK) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }

    stylesheet->native = lxb_css_stylesheet_create(NULL);
    if (stylesheet->native == NULL) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }
    status = lxb_css_stylesheet_parse(stylesheet->native,
        stylesheet->parser,
        (const lxb_char_t *)(utf8 != NULL ? utf8 : ""), length);
    if (status != LXB_STATUS_OK) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }
    stylesheet->matcher = lxb_selectors_create();
    if (stylesheet->matcher == NULL ||
        lxb_selectors_init(stylesheet->matcher) != LXB_STATUS_OK) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }
    if (!h2g_css_build_index(stylesheet)) {
        h2g_css_destroy(stylesheet);
        return NULL;
    }
    return stylesheet;
}

void h2g_css_destroy(H2GCssStylesheet *stylesheet)
{
    if (stylesheet == NULL) return;
    free(stylesheet->styles);
    if (stylesheet->matcher != NULL)
        lxb_selectors_destroy(stylesheet->matcher, 1);
    if (stylesheet->native != NULL)
        lxb_css_stylesheet_destroy(stylesheet->native, 1);
    if (stylesheet->parser != NULL)
        lxb_css_parser_destroy(stylesheet->parser, 1);
    free(stylesheet);
}

const H2GCssRule *h2g_css_first_rule(const H2GCssStylesheet *stylesheet)
{
    lxb_css_rule_list_t *list;
    if (stylesheet == NULL || stylesheet->native == NULL ||
        stylesheet->native->root == NULL) return NULL;
    list = lxb_css_rule_list(stylesheet->native->root);
    if (list == NULL) return NULL;
    return (const H2GCssRule *)list->first;
}

const H2GCssRule *h2g_css_next_rule(const H2GCssRule *rule)
{
    const lxb_css_rule_t *r = (const lxb_css_rule_t *)rule;
    return r != NULL ? (const H2GCssRule *)r->next : NULL;
}

int h2g_css_rule_kind(const H2GCssRule *rule)
{
    const lxb_css_rule_t *r = (const lxb_css_rule_t *)rule;
    if (r == NULL) return 0;
    if (r->type == LXB_CSS_RULE_STYLE) return H2G_CSS_STYLE_RULE;
    if (r->type == LXB_CSS_RULE_AT_RULE) return H2G_CSS_AT_RULE;
    return H2G_CSS_OTHER_RULE;
}

size_t h2g_css_top_level_rule_count(const H2GCssStylesheet *stylesheet)
{
    const H2GCssRule *rule;
    size_t count = 0;
    for (rule = h2g_css_first_rule(stylesheet); rule != NULL;
         rule = h2g_css_next_rule(rule)) count++;
    return count;
}

/* --- read-only renderer bridge: raw DOM data and Lexbor selector matching --- */

/* These data pointers belong to the Lexbor document. */
const H2GDomNode *h2g_html_head(const H2GHtmlDocument *document)
{
    if (document == NULL || document->native == NULL ||
        document->native->head == NULL) return NULL;
    return (const H2GDomNode *)
        lxb_dom_interface_node(document->native->head);
}

const char *h2g_dom_attribute(const H2GDomNode *node, const char *name,
                              size_t *length)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    if (length != NULL) *length = 0;
    if (n == NULL || name == NULL ||
        n->type != LXB_DOM_NODE_TYPE_ELEMENT) return NULL;
    return (const char *)lxb_dom_element_get_attribute(
        lxb_dom_interface_element((lxb_dom_node_t *)n),
        (const lxb_char_t *)name, strlen(name), length);
}

const char *h2g_dom_text(const H2GDomNode *node, size_t *length)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    const lxb_dom_text_t *t;
    if (length != NULL) *length = 0;
    if (n == NULL || n->type != LXB_DOM_NODE_TYPE_TEXT) return NULL;
    t = lxb_dom_interface_text((lxb_dom_node_t *)n);
    if (length != NULL) *length = t->char_data.data.length;
    return (const char *)t->char_data.data.data;
}

const char *h2g_dom_text_content(const H2GDomNode *node, size_t *length)
{
    if (length != NULL) *length = 0;
    if (node == NULL) return NULL;
    return (const char *)lxb_dom_node_text_content(
        (lxb_dom_node_t *)node, length);
}

void h2g_dom_text_content_free(const H2GDomNode *node, const char *text)
{
    const lxb_dom_node_t *n = (const lxb_dom_node_t *)node;
    if (n == NULL || text == NULL) return;
    lxb_dom_document_destroy_text(n->owner_document, (lxb_char_t *)text);
}

typedef struct H2GMatchResult {
    unsigned long best;
    int found;
} H2GMatchResult;

static lxb_status_t h2g_selector_hit(lxb_dom_node_t *node,
    lxb_css_selector_specificity_t specificity, void *ctx)
{
    H2GMatchResult *result = (H2GMatchResult *)ctx;
    (void)node;
    if (!result->found || (unsigned long)specificity > result->best)
        result->best = (unsigned long)specificity;
    result->found = 1;
    return LXB_STATUS_OK;
}

typedef struct H2GStringWriter {
    char *data;
    size_t used, capacity;
} H2GStringWriter;

static lxb_status_t h2g_append_css(const lxb_char_t *data,
                                    size_t length, void *ctx)
{
    H2GStringWriter *buf = (H2GStringWriter *)ctx;
    if (buf->capacity - buf->used <= length) return LXB_STATUS_ERROR;
    memcpy(buf->data + buf->used, data, length);
    buf->used += length;
    buf->data[buf->used] = '\0';
    return LXB_STATUS_OK;
}

static int h2g_css_match_indexed(const H2GCssStylesheet *stylesheet,
    const H2GIndexedRule *entry, const H2GDomNode *node,
    H2GCssDeclarationFn callback, void *user)
{
    const lxb_css_rule_style_t *style = entry->rule;
    const lxb_css_rule_t *decl;
    const lxb_css_rule_declaration_t *property;
    H2GMatchResult match;
    H2GStringWriter writer;
    lxb_status_t status;
    unsigned long index = 0;
    char name[256], value[2048];

    if (style->selector == NULL || style->declarations == NULL)
        return 1;
    match.found = 0;
    match.best = 0;
    status = lxb_selectors_match_node(stylesheet->matcher,
                (lxb_dom_node_t *)node, style->selector,
                h2g_selector_hit, &match);
    if (status != LXB_STATUS_OK) return 0;
    if (!match.found) return 1;

    for (decl = style->declarations->first;
         decl != NULL; decl = decl->next) {
        if (decl->type != LXB_CSS_RULE_DECLARATION) continue;
        property = lxb_css_rule_declaration(decl);
        writer.data = name; writer.capacity = sizeof(name);
        writer.used = 0; name[0] = '\0';
        status = lxb_css_rule_declaration_serialize_name(
              property, h2g_append_css, &writer);
        if (status != LXB_STATUS_OK) continue;
        writer.data = value; writer.capacity = sizeof(value);
        writer.used = 0; value[0] = '\0';
        status = lxb_css_property_serialize(property->u.user,
                  property->type, h2g_append_css, &writer);
        if (status != LXB_STATUS_OK) continue;
        callback(name, value, match.best, property->important ? 1 : 0,
                 entry->sequence * 4096UL + index++, user);
    }
    return 1;
}

/* Indexed selector matching is especially important for thousands of
 * Tailwind utility rules and 20,000+ repeated document nodes.
 */
int h2g_css_visit_matching(const H2GCssStylesheet *stylesheet,
      const H2GDomNode *node, H2GCssDeclarationFn callback, void *user)
{
    const char *classes, *cursor, *end;
    size_t length;
    int index;
    unsigned long bucket;
    const H2GIndexedRule *entry;

    if (stylesheet == NULL || node == NULL || callback == NULL ||
        stylesheet->native == NULL || stylesheet->matcher == NULL)
        return 0;
    if (h2g_dom_type(node) != H2G_DOM_ELEMENT) return 1;

    for (index = stylesheet->fallback; index >= 0;
         index = stylesheet->styles[index].next) {
        entry = &stylesheet->styles[index];
        if (!h2g_css_match_indexed(stylesheet, entry, node,
                                   callback, user)) return 0;
    }

    classes = h2g_dom_attribute(node, "class", &length);
    if (classes == NULL) return 1;
    cursor = classes;
    end = classes + length;
    while (cursor < end) {
        const char *begin;
        while (cursor < end && (*cursor == ' ' || *cursor == '\n' ||
               *cursor == '\t' || *cursor == '\r' || *cursor == '\f'))
            cursor++;
        begin = cursor;
        while (cursor < end && *cursor != ' ' && *cursor != '\n' &&
               *cursor != '\t' && *cursor != '\r' && *cursor != '\f')
            cursor++;
        if (begin == cursor) continue;
        bucket = h2g_class_hash(begin, (size_t)(cursor - begin));
        for (index = stylesheet->buckets[bucket]; index >= 0;
             index = stylesheet->styles[index].next) {
            entry = &stylesheet->styles[index];
            if (entry->anchor_length != (size_t)(cursor - begin) ||
                memcmp(entry->anchor, begin,
                       entry->anchor_length) != 0) continue;
            if (!h2g_css_match_indexed(stylesheet, entry, node,
                                       callback, user)) return 0;
        }
    }
    return 1;
}

size_t h2g_css_indexed_style_rule_count(const H2GCssStylesheet *stylesheet)
{
    return stylesheet != NULL ? stylesheet->styles_count : 0;
}

static int h2g_css_selector_contains_root(const lexbor_str_t *raw)
{
    const char *s;
    size_t i;
    if (raw == NULL || raw->data == NULL || raw->length < 5) return 0;
    s = (const char *)raw->data;
    for (i = 0; i + 5 <= raw->length; i++)
        if (memcmp(s + i, ":root", 5) == 0)
            return 1;
    return 0;
}

static int h2g_css_visit_bad_root(const lxb_css_rule_t *node, int depth,
             H2GCssDeclarationFn callback, void *user)
{
    const lxb_css_rule_t *rule, *decl;
    const lxb_css_rule_bad_style_t *bad;
    const lxb_css_rule_declaration_t *property;
    const lxb_css_rule_list_t *nested;
    H2GStringWriter writer;
    lxb_status_t status;
    char name[256], value[2048];
    int found = 0;
    unsigned long order = 0;

    if (depth >= 32) return 0;
    for (rule = node; rule != NULL; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_BAD_STYLE) {
            bad = lxb_css_rule_bad_style(rule);
            if (h2g_css_selector_contains_root(&bad->selectors) &&
                bad->declarations != NULL) {
                for (decl = bad->declarations->first; decl;
                     decl = decl->next) {
                    if (decl->type != LXB_CSS_RULE_DECLARATION)
                        continue;
                    property = lxb_css_rule_declaration(decl);
                    if (property->type != LXB_CSS_PROPERTY__CUSTOM)
                        continue;
                    writer.data = name; writer.capacity = sizeof(name);
                    writer.used = 0; name[0] = '\0';
                    status = lxb_css_rule_declaration_serialize_name(
                        property, h2g_append_css, &writer);
                    if (status != LXB_STATUS_OK) continue;
                    writer.data = value; writer.capacity = sizeof(value);
                    writer.used = 0; value[0] = '\0';
                    status = lxb_css_property_serialize(property->u.user,
                             property->type, h2g_append_css, &writer);
                    if (status != LXB_STATUS_OK) continue;
                    callback(name,value,1UL,
                        property->important ? 1 : 0,order++,user);
                    found++;
                }
            }
        }
        nested = h2g_layer_children(rule);
        if (nested != NULL)
            found += h2g_css_visit_bad_root(nested->first,depth+1,
                                             callback,user);
    }
    return found;
}

int h2g_css_visit_root_variables(const H2GCssStylesheet *stylesheet,
                                 H2GCssDeclarationFn callback, void *user)
{
    const lxb_css_rule_list_t *root;
    if (stylesheet == NULL || stylesheet->native == NULL ||
        stylesheet->native->root == NULL || callback == NULL)
        return 0;
    root = lxb_css_rule_list(stylesheet->native->root);
    if (root == NULL) return 0;
    return h2g_css_visit_bad_root(root->first,0,callback,user);
}
