#ifndef HTML2GUI_PARSER_H
#define HTML2GUI_PARSER_H

/* Public API is ISO C89; no third-party headers escape this boundary. */
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

H2GHtmlDocument *h2g_html_parse(const char *utf8, size_t length);
void h2g_html_destroy(H2GHtmlDocument *document);
const H2GDomNode *h2g_html_body(const H2GHtmlDocument *document);
const H2GDomNode *h2g_html_head(const H2GHtmlDocument *document);
const H2GDomNode *h2g_dom_first_child(const H2GDomNode *node);
const H2GDomNode *h2g_dom_next_sibling(const H2GDomNode *node);
const H2GDomNode *h2g_dom_parent(const H2GDomNode *node);
int h2g_dom_type(const H2GDomNode *node);
const char *h2g_dom_element_name(const H2GDomNode *node, size_t *length);
const char *h2g_dom_attribute(const H2GDomNode *node, const char *name,
                              size_t *length);
/* Text data for a text node (borrowed); use text_content for descendants. */
const char *h2g_dom_text(const H2GDomNode *node, size_t *length);
/* Text content is an allocated copy. Release with h2g_dom_text_content_free. */
const char *h2g_dom_text_content(const H2GDomNode *node, size_t *length);
void h2g_dom_text_content_free(const H2GDomNode *node, const char *text);
size_t h2g_html_body_node_count(const H2GHtmlDocument *document);

H2GCssStylesheet *h2g_css_parse(const char *utf8, size_t length);
void h2g_css_destroy(H2GCssStylesheet *stylesheet);
const H2GCssRule *h2g_css_first_rule(const H2GCssStylesheet *stylesheet);
const H2GCssRule *h2g_css_next_rule(const H2GCssRule *rule);
int h2g_css_rule_kind(const H2GCssRule *rule);
size_t h2g_css_top_level_rule_count(const H2GCssStylesheet *stylesheet);
size_t h2g_css_indexed_style_rule_count(const H2GCssStylesheet *stylesheet);
/* Callback receives serialized CSS declaration name/value and precedence.
 * Pointers are valid only during the callback. The caller implements the
 * computed style and cascade. This release matches style rules in unconditional @layer groups;
 * @media, @supports, origins and inline styles are not yet applied.
 * Returns 1 on success, 0 on internal selector failure.
 */
typedef void (*H2GCssDeclarationFn)(const char *name, const char *value,
      unsigned long specificity, int important, unsigned long order, void *user);
int h2g_css_visit_matching(const H2GCssStylesheet *stylesheet,
      const H2GDomNode *node, H2GCssDeclarationFn callback, void *user);
/* Include nested declarations guarded by supported width-only @media
 * conditions. Does not yet implement full media query semantics.
 */
int h2g_css_visit_matching_at_width(const H2GCssStylesheet *stylesheet,
      const H2GDomNode *node, float viewport_width,
      H2GCssDeclarationFn callback, void *user);

/* Recover parsed :root custom declarations from unsupported selector lists. */
int h2g_css_visit_root_variables(const H2GCssStylesheet *stylesheet,
                                 H2GCssDeclarationFn callback, void *user);

#ifdef __cplusplus
}
#endif
#endif
