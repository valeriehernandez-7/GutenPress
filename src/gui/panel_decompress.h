#ifndef PANEL_DECOMPRESS_H
#define PANEL_DECOMPRESS_H

#include <gtk/gtk.h>

// Builds the "Descomprimir" panel: .huff file chooser, method selector,
// destination folder chooser, output folder name entry, run button,
// and a result area.
GtkWidget *buildDecompressPanel(GtkStack *stack);

#endif // PANEL_DECOMPRESS_H
