#ifndef PANEL_COMPRESS_H
#define PANEL_COMPRESS_H

#include <gtk/gtk.h>

// Builds the "Comprimir" panel: input folder chooser, method selector,
// output .huff file chooser, run button, and a result area. The stack
// is needed so the panel's own "back to menu" button can navigate.
GtkWidget *buildCompressPanel(GtkStack *stack);

#endif // PANEL_COMPRESS_H
