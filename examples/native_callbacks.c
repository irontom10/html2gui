/*
 * An embeddable html2gui application: HTML/CSS define the interface,
 * and plain C functions handle actions. ISO C89.
 */
#include "html2gui/renderer.h"
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

typedef struct AppState {
    unsigned int hello_clicks;
} AppState;

static void hello_action(AppState *state)
{
    state->hello_clicks++;
    printf("Hello from native C (click %u)\n", state->hello_clicks);
}

static void second_action(AppState *state)
{
    printf("Second button; hello has been clicked %u times\n",
           state->hello_clicks);
}

static void on_button(const char *id, void *user)
{
    AppState *state = (AppState *)user;
    if (strcmp(id, "hello") == 0)
        hello_action(state);
    else if (strcmp(id, "second") == 0)
        second_action(state);
}

static void on_input(const char *id, const char *text, void *user)
{
    AppState *state = (AppState *)user;
    (void)state;
    if (strcmp(id, "name") == 0)
        printf("Native text changed: %s\n", text);
}

int main(void)
{
    AppState state;
    state.hello_clicks = 0;
    return h2g_renderer_run_ex("examples/basic.html", "examples/basic.css",
                               0, on_button, on_input, &state);
}
