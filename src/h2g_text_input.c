#include "h2g_text_input.h"
#include <stdlib.h>
#include <string.h>

static int h2g_text_grow(H2GTextInput *input, size_t needed)
{
    size_t capacity;
    char *memory;
    if (needed > (size_t)H2G_TEXT_INPUT_MAX_BYTES + 1) return 0;
    if (input->capacity >= needed) return 1;
    capacity = input->capacity != 0 ? input->capacity : 64;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity > (size_t)H2G_TEXT_INPUT_MAX_BYTES + 1)
            capacity = (size_t)H2G_TEXT_INPUT_MAX_BYTES + 1;
    }
    memory = (char *)malloc(capacity);
    if (memory == NULL) return 0;
    if (input->text != NULL) {
        memcpy(memory, input->text, input->length + 1);
        free(input->text);
    } else {
        memory[0] = '\0';
    }
    input->text = memory;
    input->capacity = capacity;
    return 1;
}

/* Cursor coordinates are byte offsets at UTF-8 codepoint boundaries. */
static int h2g_text_continuation(unsigned char byte)
{
    return (byte & 0xc0) == 0x80;
}

static size_t h2g_text_previous(const H2GTextInput *input)
{
    size_t cursor = input->cursor;
    if (cursor == 0) return 0;
    cursor--;
    while (cursor > 0 &&
           h2g_text_continuation((unsigned char)input->text[cursor]))
        cursor--;
    return cursor;
}

static size_t h2g_text_next(const H2GTextInput *input)
{
    size_t cursor = input->cursor;
    if (cursor >= input->length) return input->length;
    cursor++;
    while (cursor < input->length &&
           h2g_text_continuation((unsigned char)input->text[cursor]))
        cursor++;
    return cursor;
}

/* Reject malformed and truncated sequences rather than storing text that
 * breaks cursor movement. SDL3 supplies valid UTF-8 text input events.
 */
static int h2g_text_valid(const char *data, size_t length)
{
    size_t i = 0, count, j;
    unsigned char c, d;
    if (data == NULL) return length == 0;
    while (i < length) {
        c = (unsigned char)data[i];
        if (c < 0x80) {
            if (c == 0 || c == '\r' || c == '\n') return 0;
            i++;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) count = 2;
        else if (c >= 0xe0 && c <= 0xef) count = 3;
        else if (c >= 0xf0 && c <= 0xf4) count = 4;
        else return 0;
        if (length - i < count) return 0;
        for (j = 1; j < count; j++)
            if (!h2g_text_continuation((unsigned char)data[i + j]))
                return 0;
        d = (unsigned char)data[i + 1];
        if ((c == 0xe0 && d < 0xa0) ||
            (c == 0xed && d >= 0xa0) ||
            (c == 0xf0 && d < 0x90) ||
            (c == 0xf4 && d >= 0x90))
            return 0;
        i += count;
    }
    return 1;
}

void h2g_text_input_init(H2GTextInput *input)
{
    input->text = NULL;
    input->length = 0;
    input->cursor = 0;
    input->capacity = 0;
}

void h2g_text_input_destroy(H2GTextInput *input)
{
    free(input->text);
    h2g_text_input_init(input);
}

int h2g_text_input_set(H2GTextInput *input,
                       const char *utf8, size_t length)
{
    if (length > H2G_TEXT_INPUT_MAX_BYTES ||
        !h2g_text_valid(utf8, length) ||
        !h2g_text_grow(input, length + 1))
        return 0;
    if (length != 0) memcpy(input->text, utf8, length);
    input->text[length] = '\0';
    input->length = length;
    input->cursor = length;
    return 1;
}

int h2g_text_input_insert(H2GTextInput *input,
                          const char *utf8, size_t length)
{
    if (length == 0) return 0;
    if (length > H2G_TEXT_INPUT_MAX_BYTES - input->length ||
        !h2g_text_valid(utf8, length) ||
        !h2g_text_grow(input, input->length + length + 1))
        return 0;
    memmove(input->text + input->cursor + length,
            input->text + input->cursor,
            input->length - input->cursor + 1);
    memcpy(input->text + input->cursor, utf8, length);
    input->cursor += length;
    input->length += length;
    return 1;
}

int h2g_text_input_backspace(H2GTextInput *input)
{
    size_t previous;
    if (input->cursor == 0 || input->text == NULL) return 0;
    previous = h2g_text_previous(input);
    memmove(input->text + previous, input->text + input->cursor,
            input->length - input->cursor + 1);
    input->length -= input->cursor - previous;
    input->cursor = previous;
    return 1;
}

int h2g_text_input_delete(H2GTextInput *input)
{
    size_t next;
    if (input->cursor >= input->length || input->text == NULL) return 0;
    next = h2g_text_next(input);
    memmove(input->text + input->cursor, input->text + next,
            input->length - next + 1);
    input->length -= next - input->cursor;
    return 1;
}

int h2g_text_input_left(H2GTextInput *input)
{
    if (input->cursor == 0 || input->text == NULL) return 0;
    input->cursor = h2g_text_previous(input);
    return 1;
}

int h2g_text_input_right(H2GTextInput *input)
{
    if (input->cursor >= input->length || input->text == NULL) return 0;
    input->cursor = h2g_text_next(input);
    return 1;
}

void h2g_text_input_home(H2GTextInput *input)
{
    input->cursor = 0;
}

void h2g_text_input_end(H2GTextInput *input)
{
    input->cursor = input->length;
}
