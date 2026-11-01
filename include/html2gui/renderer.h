#ifndef HTML2GUI_RENDERER_H
#define HTML2GUI_RENDERER_H

/* Public native GUI entrypoint; ISO C89 and no SDL/Lexbor types exposed. */
#ifdef __cplusplus
extern "C" {
#endif

/* Called synchronously for a clicked HTML <button> with an id attribute.
 * An unnamed button passes the empty string. The pointer is borrowed and
 * is valid for the duration of the callback only.
 */
typedef void (*H2GButtonCallback)(const char *button_id, void *user);

/* Create a window, render an HTML document with optional external CSS,
 * handle events, and destroy resources on exit. headless renders one frame.
 * Returns zero on success. This function owns the SDL event loop.
 *
 * If on_button is NULL, the command-line demo prints button IDs to stdout.
 */
int h2g_renderer_run(const char *html_path, const char *css_path,
                     int headless, H2GButtonCallback on_button, void *user);

#ifdef __cplusplus
}
#endif
#endif
