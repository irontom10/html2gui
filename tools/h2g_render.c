#include "html2gui/renderer.h"
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *html_path;
    const char *css_path = NULL;
    int headless = 0;
    int i = 1;
    if (i < argc && strcmp(argv[i], "--headless") == 0) {
        headless = 1;
        i++;
    }
    if (argc - i < 1 || argc - i > 2) {
        fprintf(stderr, "usage: %s [--headless] page.html [style.css]\n",
                argv[0]);
        return 2;
    }
    html_path = argv[i++];
    if (i < argc) css_path = argv[i];
    return h2g_renderer_run(html_path, css_path, headless, NULL, NULL);
}
