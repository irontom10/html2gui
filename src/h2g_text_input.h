#ifndef H2G_TEXT_INPUT_H
#define H2G_TEXT_INPUT_H

/* C89 single-line UTF-8 editing state, independent of SDL and Lexbor.
 * No realloc: growing buffers use malloc/copy/free.
 */
#include <stddef.h>

#define H2G_TEXT_INPUT_MAX_BYTES 4096

typedef struct H2GTextInput {
    char *text;
    size_t length;
    size_t cursor;
    size_t capacity;
} H2GTextInput;

void h2g_text_input_init(H2GTextInput *input);
void h2g_text_input_destroy(H2GTextInput *input);
int h2g_text_input_set(H2GTextInput *input,
                       const char *utf8, size_t length);
int h2g_text_input_insert(H2GTextInput *input,
                          const char *utf8, size_t length);
int h2g_text_input_backspace(H2GTextInput *input);
int h2g_text_input_delete(H2GTextInput *input);
int h2g_text_input_left(H2GTextInput *input);
int h2g_text_input_right(H2GTextInput *input);
void h2g_text_input_home(H2GTextInput *input);
void h2g_text_input_end(H2GTextInput *input);

#endif
