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

struct h2g_css_stylesheet {
    lxb_css_parser_t *parser;
    lxb_css_stylesheet_t *native;
    lxb_selectors_t *matcher;
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
    return stylesheet;
}

void h2g_css_destroy(H2GCssStylesheet *stylesheet)
{
    if (stylesheet == NULL) return;
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

int h2g_css_visit_matching(const H2GCssStylesheet *stylesheet,
      const H2GDomNode *node, H2GCssDeclarationFn callback, void *user)
{
    const lxb_css_rule_t *rule, *decl;
    const lxb_css_rule_style_t *style;
    H2GMatchResult match;
    H2GStringWriter writer;
    lxb_status_t status;
    unsigned long order = 0;
    char name[256], value[2048];

    if (stylesheet == NULL || node == NULL || callback == NULL ||
        stylesheet->native == NULL || stylesheet->matcher == NULL)
        return 0;
    if (h2g_dom_type(node) != H2G_DOM_ELEMENT) return 1;

    for (rule = (const lxb_css_rule_t *)h2g_css_first_rule(stylesheet);
         rule != NULL; rule = rule->next) {
        if (rule->type != LXB_CSS_RULE_STYLE) continue;
        style = lxb_css_rule_style(rule);
        if (style->selector == NULL || style->declarations == NULL)
            continue;

        match.found = 0; match.best = 0;
        status = lxb_selectors_match_node(stylesheet->matcher,
                    (lxb_dom_node_t *)node, style->selector,
                    h2g_selector_hit, &match);
        if (status != LXB_STATUS_OK) return 0;
        if (!match.found) continue;
        for (decl = style->declarations->first;
             decl != NULL; decl = decl->next) {
            const lxb_css_rule_declaration_t *property;
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
            callback(name, value, match.best,
                     property->important ? 1 : 0, order++, user);
        }
    }
    return 1;
}
