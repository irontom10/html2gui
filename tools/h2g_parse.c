/* Diagnostic entrypoint for the parser-only bootstrap. */
#include "html2gui/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *length)
{
    FILE *in;
    char *buffer;
    long n;
    size_t got;
    in = fopen(path, "rb");
    if (in == NULL) return NULL;
    if (fseek(in, 0L, SEEK_END) != 0) { fclose(in); return NULL; }
    n = ftell(in);
    if (n < 0 || fseek(in, 0L, SEEK_SET) != 0) {
        fclose(in);
        return NULL;
    }
    buffer = (char *)malloc((size_t)n + 1);
    if (buffer == NULL) { fclose(in); return NULL; }
    got = fread(buffer, 1, (size_t)n, in);
    fclose(in);
    if (got != (size_t)n) { free(buffer); return NULL; }
    buffer[got] = '\0';
    *length = got;
    return buffer;
}

int main(int argc, char **argv)
{
    char *buffer;
    size_t length;
    H2GHtmlDocument *html;
    H2GCssStylesheet *css;
    if (argc != 3 || (strcmp(argv[1], "html") != 0 &&
                      strcmp(argv[1], "css") != 0)) {
        fprintf(stderr, "usage: %s html|css path\n", argv[0]);
        return 2;
    }
    buffer = read_file(argv[2], &length);
    if (buffer == NULL) {
        fprintf(stderr, "unable to read %s\n", argv[2]);
        return 1;
    }
    if (strcmp(argv[1], "html") == 0) {
        html = h2g_html_parse(buffer, length);
        if (html == NULL) {
            fprintf(stderr, "HTML parser failed\n");
            free(buffer);
            return 1;
        }
        printf("HTML parsed; body nodes: %lu\n",
               (unsigned long)h2g_html_body_node_count(html));
        h2g_html_destroy(html);
    } else {
        css = h2g_css_parse(buffer, length);
        if (css == NULL) {
            fprintf(stderr, "CSS parser failed\n");
            free(buffer);
            return 1;
        }
        printf("CSS parsed; top-level rules: %lu\n",
               (unsigned long)h2g_css_top_level_rule_count(css));
        h2g_css_destroy(css);
    }
    free(buffer);
    return 0;
}
