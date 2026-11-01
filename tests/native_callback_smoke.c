#include "html2gui/renderer.h"
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

static void button_action(const char *id, void *user)
{
    int *clicks = (int *)user;
    if (strcmp(id, "hello") == 0) (*clicks)++;
}

int main(void)
{
    int clicks = 0;
    int result = h2g_renderer_run("examples/basic.html", "examples/basic.css",
                                  1, button_action, &clicks);
    if (result != 0 || clicks != 1) {
        fprintf(stderr, "native button dispatch failed: result=%d clicks=%d\n",
                result, clicks);
        return 1;
    }
    puts("native button callback: PASS");
    return 0;
}
