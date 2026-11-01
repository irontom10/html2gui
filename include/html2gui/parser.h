#ifndef HTML2GUI_PARSER_H
#define HTML2GUI_PARSER_H

/* Public API uses ISO C89 and does not expose dependency-specific headers.
 * Input is UTF-8; buffers need not be NUL-terminated.
 * Returned node/rule pointers are borrowed and die with their owner.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct h2g_html_document H2GHtmlDocument;
typedef struct h2g_css_stylesheet H2GCssStylesheet;
typedef struct h2g_dom_node H2GDomNode;
typedef struct h2g_css_rule H2GCssRule;

#define H2G_DOM_ELEMENT 1
#define H2G_DOM_TEXT 3
#define H2G_DOM_COMMENT 8
#define H2G_DOM_DOCUMENT 9
#define H2G_DOM_DOCTYPE 10

#define H2G_CSS_STYLE_RULE 1
#define H2G_CSS_AT_RULE 2
#define H2G_CSS_OTHER_RULE 3

/* Return NULL on allocation/internal parsing failure, not on recoverable
 * HTML/CSS parse errors (those are handled by the standards parser).
 */
H2GHtmlDocument *h2g_html_parse(const char *utf8, size_t length);
void h2g_html_destroy(H2GHtmlDocument *document);

/* The HTML body is created even when omitted in the input. */
const H2GDomNode *h2g_html_body(const H2GHtmlDocument *document);
const H2GDomNode *h2g_dom_first_child(const H2GDomNode *node);
const H2GDomNode *h2g_dom_next_sibling(const H2GDomNode *node);
const H2GDomNode *h2g_dom_parent(const H2GDomNode *node);
int h2g_dom_type(const H2GDomNode *node);
/* Name points into the document's memory. Non-elements return NULL. */
const char *h2g_dom_element_name(const H2GDomNode *node, size_t *length);
size_t h2g_html_body_node_count(const H2GHtmlDocument *document);

H2GCssStylesheet *h2g_css_parse(const char *utf8, size_t length);
void h2g_css_destroy(H2GCssStylesheet *stylesheet);
const H2GCssRule *h2g_css_first_rule(const H2GCssStylesheet *stylesheet);
const H2GCssRule *h2g_css_next_rule(const H2GCssRule *rule);
int h2g_css_rule_kind(const H2GCssRule *rule);
size_t h2g_css_top_level_rule_count(const H2GCssStylesheet *stylesheet);

#ifdef __cplusplus
}
#endif
#endif
