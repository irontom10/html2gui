#include "h2g_text_input.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { \
    if (!(test)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while (0)

int main(void)
{
    H2GTextInput input;
    static const char accented[] = "caf\303\251";
    static const char emoji[] = "\360\237\232\200";
    static const char malformed[] = "\303\050";
    h2g_text_input_init(&input);
    CHECK(h2g_text_input_set(&input, accented, sizeof(accented) - 1));
    CHECK(input.cursor == sizeof(accented) - 1);
    CHECK(h2g_text_input_left(&input));
    CHECK(input.cursor == 3);
    CHECK(h2g_text_input_insert(&input, " ", 1));
    CHECK(strcmp(input.text, "caf \303\251") == 0);
    CHECK(h2g_text_input_right(&input));
    CHECK(h2g_text_input_backspace(&input));
    CHECK(strcmp(input.text, "caf ") == 0);
    CHECK(h2g_text_input_insert(&input, emoji, sizeof(emoji) - 1));
    CHECK(h2g_text_input_left(&input));
    CHECK(h2g_text_input_delete(&input));
    CHECK(strcmp(input.text, "caf ") == 0);
    h2g_text_input_home(&input);
    CHECK(!h2g_text_input_backspace(&input));
    CHECK(h2g_text_input_delete(&input));
    CHECK(strcmp(input.text, "af ") == 0);
    CHECK(!h2g_text_input_insert(&input, malformed,
                                 sizeof(malformed) - 1));
    CHECK(strcmp(input.text, "af ") == 0);
    h2g_text_input_end(&input);
    CHECK(!h2g_text_input_delete(&input));
    CHECK(h2g_text_input_set(&input, "", 0));
    CHECK(input.length == 0 && input.cursor == 0);
    CHECK(input.text[0] == '\0');
    h2g_text_input_destroy(&input);
    puts("text input editing: PASS");
    return 0;
}
