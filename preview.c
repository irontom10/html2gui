/* Minimal native GTK4 preview runner for generated .ui + .css. MIT. */
#include <gtk/gtk.h>
#include "h2g_layout.h"
#include <stdio.h>

static void on_destroy(GtkWidget *window, gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    (void)window;
    g_main_loop_quit(loop);
}

int main(int argc, char **argv)
{
    GtkBuilder *builder;
    GtkCssProvider *provider;
    GtkWidget *window;
    GdkDisplay *display;
    GMainLoop *loop;
    GError *error = NULL;
    if (argc != 3) {
        fprintf(stderr, "usage: %s output.ui output.css\n", argv[0]);
        return 2;
    }
    if (!gtk_init_check()) {
        fprintf(stderr, "GTK could not initialize a display\n");
        return 1;
    }
    builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, argv[1], &error)) {
        fprintf(stderr, "Builder error: %s\n", error->message);
        g_error_free(error);
        g_object_unref(builder);
        return 1;
    }
    window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) {
        fprintf(stderr, "main_window not found in %s\n", argv[1]);
        g_object_unref(builder);
        return 1;
    }
    /* Install native max-size layout managers on generated clamp wrappers. */
    h2g_apply_layout(builder);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, argv[2]);
    display = gtk_widget_get_display(window);
    if (!display) display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), loop);
    gtk_window_present(GTK_WINDOW(window));
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    gtk_window_destroy(GTK_WINDOW(window));
    g_object_unref(provider);
    g_object_unref(builder);
    return 0;
}
