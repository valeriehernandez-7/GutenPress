#include <gtk/gtk.h>
#include "panel_compress.h"
#include "panel_decompress.h"
#include "panel_compare_compress.h"

static GtkWidget *loadIconImage(const char *iconFileName, int pixelSize)
{
    char iconPath[256];
    snprintf(iconPath, sizeof(iconPath), "icons/%s", iconFileName);

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(iconPath, pixelSize, pixelSize, TRUE, NULL);
    if (pixbuf == NULL)
    {
        fprintf(stderr, "Warning: could not load icon '%s'\n", iconPath);
        return gtk_image_new();
    }

    GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return image;
}

// Builds one clickable card for the 2x2 menu grid. valign CENTER on the
// inner box is what keeps the icon/title/subtitle centered vertically
// even when the card itself is much taller than its content.
static GtkWidget *buildMenuCard(const char *iconFileName, const char *title, const char *subtitle)
{
    GtkWidget *button = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "menu-card");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *icon = loadIconImage(iconFileName, 40);
    GtkWidget *titleLabel = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "card-title");

    GtkWidget *subtitleLabel = gtk_label_new(subtitle);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitleLabel), "card-subtitle");
    gtk_label_set_justify(GTK_LABEL(subtitleLabel), GTK_JUSTIFY_CENTER);

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), titleLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), subtitleLabel, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(button), box);
    return button;
}

static void onBackButtonClicked(GtkButton *button, gpointer userData)
{
    GtkStack *stack = GTK_STACK(userData);
    const char *targetPage = g_object_get_data(G_OBJECT(button), "target-page");
    gtk_stack_set_visible_child_name(stack, targetPage);
}

// Every non-compress stub page shares this header shape: back button,
// a small icon identifying the page, and the title - the same visual
// pattern buildCompressPanel now follows for consistency.
static GtkWidget *buildStubPanel(GtkStack *stack, const char *iconFileName, const char *title, const char *placeholderText)
{
    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(container, 20);
    gtk_widget_set_margin_end(container, 20);
    gtk_widget_set_margin_top(container, 20);

    GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *backButton = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(backButton), "back-button");
    gtk_container_add(GTK_CONTAINER(backButton), loadIconImage("back.svg", 18));
    g_object_set_data(G_OBJECT(backButton), "target-page", (gpointer) "menu");
    g_signal_connect(backButton, "clicked", G_CALLBACK(onBackButtonClicked), stack);

    GtkWidget *pageIcon = loadIconImage(iconFileName, 22);
    GtkWidget *titleLabel = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "panel-title");

    gtk_box_pack_start(GTK_BOX(headerBox), backButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerBox), pageIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerBox), titleLabel, FALSE, FALSE, 0);

    GtkWidget *placeholder = gtk_label_new(placeholderText);

    gtk_box_pack_start(GTK_BOX(container), headerBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), placeholder, FALSE, FALSE, 12);

    return container;
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    GtkCssProvider *cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(cssProvider, "style.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GutenPress");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 420);
    gtk_container_set_border_width(GTK_CONTAINER(window), 16);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *menuGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(menuGrid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(menuGrid), 12);
    gtk_grid_set_row_homogeneous(GTK_GRID(menuGrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(menuGrid), TRUE);

    GtkWidget *compressCard = buildMenuCard("compress.svg", "Comprimir", "Elige un método y comprime un directorio");
    GtkWidget *decompressCard = buildMenuCard("decompress.svg", "Descomprimir", "Elige un método y descomprime archivo .huff");
    GtkWidget *compareCompressCard = buildMenuCard("scan-compress.svg", "Analizar Compresión", "Corre los métodos disponibles y compara resultados");
    GtkWidget *compareDecompressCard = buildMenuCard("scan-decompress.svg", "Analizar Descompresión", "Corre los métodos disponibles y compara resultados");

    gtk_grid_attach(GTK_GRID(menuGrid), compressCard, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(menuGrid), decompressCard, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(menuGrid), compareCompressCard, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(menuGrid), compareDecompressCard, 1, 1, 1, 1);

    gtk_stack_add_named(GTK_STACK(stack), menuGrid, "menu");

    GtkWidget *compressPage = buildCompressPanel(GTK_STACK(stack));
    GtkWidget *decompressPage = buildDecompressPanel(GTK_STACK(stack));
    GtkWidget *compareCompressPage = buildCompareCompressPanel(GTK_STACK(stack));
    GtkWidget *compareDecompressPage = buildStubPanel(GTK_STACK(stack), "scan-decompress.svg", "Analizar Descompresión", "(panel pendiente)");

    gtk_stack_add_named(GTK_STACK(stack), compressPage, "compress");
    gtk_stack_add_named(GTK_STACK(stack), decompressPage, "decompress");
    gtk_stack_add_named(GTK_STACK(stack), compareCompressPage, "compare_compress");
    gtk_stack_add_named(GTK_STACK(stack), compareDecompressPage, "compare_decompress");

    g_signal_connect(compressCard, "clicked", G_CALLBACK(onBackButtonClicked), stack);
    g_object_set_data(G_OBJECT(compressCard), "target-page", (gpointer) "compress");

    g_signal_connect(decompressCard, "clicked", G_CALLBACK(onBackButtonClicked), stack);
    g_object_set_data(G_OBJECT(decompressCard), "target-page", (gpointer) "decompress");

    g_signal_connect(compareCompressCard, "clicked", G_CALLBACK(onBackButtonClicked), stack);
    g_object_set_data(G_OBJECT(compareCompressCard), "target-page", (gpointer) "compare_compress");

    g_signal_connect(compareDecompressCard, "clicked", G_CALLBACK(onBackButtonClicked), stack);
    g_object_set_data(G_OBJECT(compareDecompressCard), "target-page", (gpointer) "compare_decompress");

    gtk_container_add(GTK_CONTAINER(window), stack);
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
