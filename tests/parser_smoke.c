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


typedef struct CountMatches { int count; int background; int variables; } CountMatches;

static void count_matches(const char *name, const char *value,
    unsigned long specificity, int important, unsigned long order, void *user)
{
    CountMatches *count = (CountMatches *)user;
    (void)specificity; (void)important; (void)order;
    count->count++;
    if (name[0] == '-' && name[1] == '-')
        fprintf(stderr,"root custom: %s = %.100s\\n",name,value);
    if (strcmp(name,"background-color")==0 && value[0] != '\0')
        count->background++;
    if (strcmp(name,"--color-slate-950")==0 && strstr(value,"oklch")!=NULL)
        count->variables++;
}

static int test_nested_css(void)
{
    static const char markup[] = "<div class=\"bg-a\"><p>Hello</p></div>";
    static const char rules[] =
        "@layer theme { :root { --color-a: #112233; } }"
        "@layer utilities {"
        " .bg-a { background-color: #112233; }"
        " .bg-b:hover { color: red; }"
        " }";
    H2GHtmlDocument *doc;
    H2GCssStylesheet *sheet;
    const H2GDomNode *div;
    CountMatches count;
    doc = h2g_html_parse(markup,sizeof(markup)-1);
    sheet = h2g_css_parse(rules,sizeof(rules)-1);
    CHECK(doc != NULL && sheet != NULL);
    CHECK(h2g_css_indexed_style_rule_count(sheet) >= 3);
    div=find_element(h2g_html_body(doc),"div");
    CHECK(div != NULL);
    count.count=0; count.background=0; count.variables=0;
    CHECK(h2g_css_visit_matching(sheet,div,count_matches,&count));
    CHECK(count.background==1);
    h2g_css_destroy(sheet);
    h2g_html_destroy(doc);
    return 0;
}


static int test_tailwind_root_variables(void)
{
    static const char markup[] =
        "<!doctype html><html><head></head><body><div>content</div></body></html>";
    static const char css[] =
        "@layer theme { :root, :host {"
        " --color-slate-950: oklch(12.9% 0.042 264.695);"
        " --spacing: 0.25rem;"
        " } }";
    H2GHtmlDocument *doc = h2g_html_parse(markup, sizeof(markup)-1);
    H2GCssStylesheet *sheet = h2g_css_parse(css, sizeof(css)-1);
    const H2GDomNode *root;
    CountMatches count;
    CHECK(doc != NULL && sheet != NULL);
    root = h2g_dom_parent(h2g_html_head(doc));
    CHECK(root != NULL);
    count.count=0; count.background=0; count.variables=0;
    CHECK(h2g_css_visit_matching(sheet,root,count_matches,&count));
    (void)h2g_css_visit_root_variables(sheet,count_matches,&count);
    fprintf(stderr,"root matched declarations=%d, variables=%d, indexed=%lu\\n",
            count.count,count.variables,
            (unsigned long)h2g_css_indexed_style_rule_count(sheet));
    CHECK(count.variables==1);
    h2g_css_destroy(sheet);
    h2g_html_destroy(doc);
    return 0;
}

int main(void)
{
    CHECK(test_html() == 0);
    CHECK(test_css() == 0);
    CHECK(test_nested_css() == 0);
    CHECK(test_tailwind_root_variables() == 0);
    puts("parser smoke tests: PASS");
    return 0;
}
