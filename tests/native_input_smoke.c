#include "html2gui/renderer.h"
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestState {
    int callback_count;
    int correct_value;
} TestState;

static void input_changed(const char *id, const char *value, void *user)
{
    TestState *state = (TestState *)user;
    state->callback_count++;
    if (strcmp(id, "name") == 0) {
        const char *left=getenv("H2G_TEST_INPUT_X");
        const char *expected=left != NULL ? "abcseed" : "seedabc";
        if (strcmp(value,expected) == 0)
            state->correct_value=1;
    }
}

int main(void)
{
    TestState state;
    int result;
    state.callback_count=0;
    state.correct_value=0;
    result=h2g_renderer_run_ex("examples/basic.html","examples/basic.css",
                               1,NULL,input_changed,&state);
    if (result != 0 || state.callback_count != 1 ||
        !state.correct_value) {
        fprintf(stderr,"input callback FAIL result=%d calls=%d value=%d\n",
                result,state.callback_count,state.correct_value);
        return 1;
    }
    puts("native input callback: PASS");
    return 0;
}
