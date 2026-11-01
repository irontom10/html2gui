/*
 * Native max-width/max-height clamp for generated GtkBuilder boxes.
 * C89 implementation using GTK4's stock GtkCustomLayout manager.
 * This never injects scrollbars into max-sized content.
 * The generated XML stays loadable and valid with ordinary GTK4 types.
 */
#include "h2g_layout.h"
#include <stdio.h>
#include <string.h>

static int h2g_cap(GtkWidget *w, const char *key)
{
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), key)) - 1;
}

static GtkSizeRequestMode h2g_request(GtkWidget *w)
{
    GtkWidget *child = gtk_widget_get_first_child(w);
    if (child) return gtk_widget_get_request_mode(child);
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void h2g_measure(GtkWidget *w, GtkOrientation orient, int for_size,
                        int *minimum, int *natural,
                        int *minimum_baseline, int *natural_baseline)
{
    GtkWidget *child = gtk_widget_get_first_child(w);
    int capw = h2g_cap(w, "h2g-max-w");
    int caph = h2g_cap(w, "h2g-max-h");
    int cap = orient == GTK_ORIENTATION_HORIZONTAL ? capw : caph;
    int cross = orient == GTK_ORIENTATION_HORIZONTAL ? caph : capw;
    if (!child) {
        *minimum = *natural = 0;
        *minimum_baseline = *natural_baseline = -1;
        return;
    }
    if (cross >= 0 && (for_size < 0 || for_size > cross)) for_size = cross;
    gtk_widget_measure(child, orient, for_size, minimum, natural,
                       minimum_baseline, natural_baseline);
    if (cap >= 0) {
        if (*minimum > cap) *minimum = cap;
        if (*natural > cap) *natural = cap;
    }
}

static void h2g_allocate(GtkWidget *w, int width, int height, int baseline)
{
    GtkWidget *child = gtk_widget_get_first_child(w);
    int capw = h2g_cap(w, "h2g-max-w");
    int caph = h2g_cap(w, "h2g-max-h");
    int center = h2g_cap(w, "h2g-center") > 0;
    int cw = width, ch = height, x = 0;
    graphene_point_t point;
    GskTransform *transform;
    (void)baseline;
    if (!child) return;
    if (capw >= 0 && cw > capw) cw = capw;
    if (caph >= 0 && ch > caph) ch = caph;
    if (center && cw < width) x = (width - cw) / 2;
    graphene_point_init(&point, (float)x, 0.0f);
    transform = gsk_transform_translate(NULL, &point);
    gtk_widget_allocate(child, cw, ch, -1, transform);
}

void h2g_apply_layout(GtkBuilder *builder)
{
    GSList *items, *p;
    const char *id;
    int index, maxw, maxh, center;
    GtkLayoutManager *manager;
    GtkWidget *w;
    items = gtk_builder_get_objects(builder);
    for (p = items; p; p = p->next) {
        if (!GTK_IS_WIDGET(p->data)) continue;
        w = GTK_WIDGET(p->data);
        id = gtk_buildable_get_buildable_id(GTK_BUILDABLE(w));
        if (!id || strncmp(id, "h2g_clamp_", 10) != 0) continue;
        if (sscanf(id, "h2g_clamp_%d_%d_%d_%d", &index,
                   &maxw, &maxh, &center) != 4) continue;
        (void)index;
        g_object_set_data(G_OBJECT(w), "h2g-max-w", GINT_TO_POINTER(maxw + 1));
        g_object_set_data(G_OBJECT(w), "h2g-max-h", GINT_TO_POINTER(maxh + 1));
        g_object_set_data(G_OBJECT(w), "h2g-center", GINT_TO_POINTER(center + 1));
        manager = gtk_custom_layout_new(h2g_request, h2g_measure, h2g_allocate);
        gtk_widget_set_layout_manager(w, manager);
    }
    g_slist_free(items);
}
