#include "html2gui/parser.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test) do { \
    if (!(test)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while (0)

static const H2GDomNode *find_element(const H2GDomNode *node,
                                     const char *name)
{
    const H2GDomNode *child;
    const char *tag;
    size_t n;
    if (node == NULL) return NULL;
    tag = h2g_dom_element_name(node, &n);
    if (tag != NULL && strlen(name) == n &&
        memcmp(tag, name, n) == 0) return node;
    child = h2g_dom_first_child(node);
    while (child != NULL) {
        const H2GDomNode *found = find_element(child, name);
        if (found != NULL) return found;
        child = h2g_dom_next_sibling(child);
    }
    return NULL;
}

static int test_html(void)
{
    static const char html[] =
        "<!DOCTYPE html><title>Demo</title>"
        "<div class=test><p>one<p>two &NotEqualTilde;</div>"
        "<table><tr><td>A</td></tr></table>"
        "<svg><circle r='3'/></svg>";
    H2GHtmlDocument *doc = h2g_html_parse(html, sizeof(html)-1);
    const H2GDomNode *body;
    CHECK(doc != NULL);
    body = h2g_html_body(doc);
    CHECK(body != NULL);
    CHECK(h2g_dom_type(body) == H2G_DOM_ELEMENT);
    CHECK(find_element(body, "div") != NULL);
    CHECK(find_element(body, "p") != NULL);
    CHECK(find_element(body, "table") != NULL);
    CHECK(find_element(body, "svg") != NULL);
    CHECK(h2g_html_body_node_count(doc) >= 10);
    h2g_html_destroy(doc);

    doc = h2g_html_parse("<p>unclosed", strlen("<p>unclosed"));
    CHECK(doc != NULL);
    CHECK(find_element(h2g_html_body(doc), "p") != NULL);
    h2g_html_destroy(doc);
    return 0;
}

static int test_css(void)
{
    static const char css[] =
        "/* comments */ .demo:is(div, p) {"
        " --accent: color-mix(in oklch, red 20%, blue);"
        " color: var(--accent) !important; }"
        "@media (width >= 500px) {"
        " .demo { display: grid; grid-template-columns: 1fr 2fr; }"
        "}";
    static const char bad_css[] = ".x { color: ; broken :::; }";
    H2GCssStylesheet *sheet = h2g_css_parse(css, sizeof(css)-1);
    const H2GCssRule *first;
    CHECK(sheet != NULL);
    CHECK(h2g_css_top_level_rule_count(sheet) == 2);
    first = h2g_css_first_rule(sheet);
    CHECK(first != NULL);
    CHECK(h2g_css_rule_kind(first) == H2G_CSS_STYLE_RULE);
    CHECK(h2g_css_rule_kind(h2g_css_next_rule(first)) == H2G_CSS_AT_RULE);
    h2g_css_destroy(sheet);

    sheet = h2g_css_parse(bad_css, sizeof(bad_css)-1);
    CHECK(sheet != NULL);
    h2g_css_destroy(sheet);
    return 0;
}

int main(void)
{
    CHECK(test_html() == 0);
    CHECK(test_css() == 0);
    puts("parser smoke tests: PASS");
    return 0;
}
