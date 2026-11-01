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

struct h2g_html_document {
    lxb_html_document_t *native;
};

struct h2g_css_stylesheet {
    lxb_css_parser_t *parser;
    lxb_css_stylesheet_t *native;
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
    return stylesheet;
}

void h2g_css_destroy(H2GCssStylesheet *stylesheet)
{
    if (stylesheet == NULL) return;
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
