#define _POSIX_C_SOURCE 200809L

#include "panel_decompress.h"
#include "process_runner.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *label;
  const char *executablePath;
} MethodOption;

static const MethodOption DECOMPRESS_METHODS[] = {
    {"Secuencial (serial)", "../serial/serial_decompressor"},
    {"Paralelo por procesos (fork + IPC)", "../fork_ipc/fork_decompressor"},
    {"Paralelo por hilos (pthreads)", "../pthreads/pthread_decompressor"},
};
#define DECOMPRESS_METHOD_COUNT 3

typedef struct {
  GtkWidget *archiveFileChooser;
  GtkWidget *methodCombo;
  GtkWidget *destinationFolderChooser;
  GtkWidget *outputNameEntry;
  GtkWidget *runButton;
  GtkWidget *spinner;
  GtkWidget *resultLabel;
} DecompressPanelWidgets;

typedef struct {
  DecompressPanelWidgets *widgets;
  ProcessResult result;
} DecompressResultMessage;

// Builds "<destinationFolder>/<enteredName>", the folder the archive
// gets expanded into — following the name/ convention agreed for
// decompression output, mirroring name.huff for compression.
static char *buildOutputFolderPath(const char *destinationFolder,
                                   const char *enteredName) {
  char *outputPath =
      malloc(strlen(destinationFolder) + 1 + strlen(enteredName) + 1);
  sprintf(outputPath, "%s/%s", destinationFolder, enteredName);
  return outputPath;
}

static gboolean applyDecompressResultOnMainThread(gpointer userData) {
  DecompressResultMessage *message = (DecompressResultMessage *)userData;
  DecompressPanelWidgets *widgets = message->widgets;

  gtk_spinner_stop(GTK_SPINNER(widgets->spinner));
  gtk_widget_set_sensitive(widgets->runButton, TRUE);

  if (message->result.wasSuccessful) {
    char labelText[2200];
    snprintf(labelText, sizeof(labelText),
             "Descompresión completada en %.3f segundos.\n\n%s",
             message->result.elapsedSeconds,
             message->result.capturedOutputTail);
    gtk_label_set_text(GTK_LABEL(widgets->resultLabel), labelText);
    gtk_style_context_remove_class(
        gtk_widget_get_style_context(widgets->resultLabel), "status-error");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(widgets->resultLabel), "status-success");
  } else {
    char labelText[2200];
    snprintf(labelText, sizeof(labelText),
             "La descompresión falló después de %.3f segundos.\n\n%s",
             message->result.elapsedSeconds,
             message->result.capturedOutputTail);
    gtk_label_set_text(GTK_LABEL(widgets->resultLabel), labelText);
    gtk_style_context_remove_class(
        gtk_widget_get_style_context(widgets->resultLabel), "status-success");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(widgets->resultLabel), "status-error");
  }

  free(message);
  return G_SOURCE_REMOVE;
}

static void *runDecompressionInBackground(void *argument) {
  DecompressResultMessage *message = (DecompressResultMessage *)argument;
  DecompressPanelWidgets *widgets = message->widgets;

  char *archivePath = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  char *destinationFolder = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->destinationFolderChooser));
  const char *enteredName =
      gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));
  char *outputFolderPath =
      buildOutputFolderPath(destinationFolder, enteredName);

  int methodIndex =
      gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->methodCombo));
  const char *executablePath = DECOMPRESS_METHODS[methodIndex].executablePath;

  char *argv[] = {(char *)executablePath, archivePath, outputFolderPath, NULL};
  message->result = runProcessAndMeasureTime(executablePath, argv);

  g_free(archivePath);
  g_free(destinationFolder);
  free(outputFolderPath);

  g_idle_add(applyDecompressResultOnMainThread, message);
  return NULL;
}

static void onDecompressButtonClicked(GtkButton *button, gpointer userData) {
  (void)button;
  DecompressPanelWidgets *widgets = (DecompressPanelWidgets *)userData;

  char *archivePath = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  char *destinationFolder = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->destinationFolderChooser));
  const char *enteredName =
      gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));

  if (archivePath == NULL || destinationFolder == NULL ||
      strlen(enteredName) == 0) {
    gtk_label_set_text(GTK_LABEL(widgets->resultLabel),
                       "Selecciona el archivo .huff, la carpeta destino, y un "
                       "nombre para la carpeta resultante.");
    g_free(archivePath);
    g_free(destinationFolder);
    return;
  }
  g_free(archivePath);
  g_free(destinationFolder);

  gtk_widget_set_sensitive(widgets->runButton, FALSE);
  gtk_spinner_start(GTK_SPINNER(widgets->spinner));
  gtk_label_set_text(GTK_LABEL(widgets->resultLabel),
                     "Descomprimiendo, por favor espera...");
  gtk_style_context_remove_class(
      gtk_widget_get_style_context(widgets->resultLabel), "status-success");
  gtk_style_context_remove_class(
      gtk_widget_get_style_context(widgets->resultLabel), "status-error");

  DecompressResultMessage *message = malloc(sizeof(DecompressResultMessage));
  message->widgets = widgets;

  pthread_t backgroundThread;
  pthread_create(&backgroundThread, NULL, runDecompressionInBackground,
                 message);
  pthread_detach(backgroundThread);
}

static void onBackToMenuClicked(GtkButton *button, gpointer userData) {
  (void)button;
  gtk_stack_set_visible_child_name(GTK_STACK(userData), "menu");
}

static GtkWidget *loadPanelIcon(const char *iconFileName, int pixelSize) {
  char iconPath[256];
  snprintf(iconPath, sizeof(iconPath), "icons/%s", iconFileName);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(iconPath, pixelSize,
                                                        pixelSize, TRUE, NULL);
  if (pixbuf == NULL) {
    return gtk_image_new();
  }
  GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
  g_object_unref(pixbuf);
  return image;
}

static void onDecompressPanelMapped(GtkWidget *widget, gpointer userData) {
  (void)widget;
  DecompressPanelWidgets *widgets = (DecompressPanelWidgets *)userData;

  gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  gtk_file_chooser_unselect_all(
      GTK_FILE_CHOOSER(widgets->destinationFolderChooser));
  gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "extracted");
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->methodCombo), 0);
  gtk_label_set_text(GTK_LABEL(widgets->resultLabel), "");
  gtk_style_context_remove_class(
      gtk_widget_get_style_context(widgets->resultLabel), "status-success");
  gtk_style_context_remove_class(
      gtk_widget_get_style_context(widgets->resultLabel), "status-error");
}

GtkWidget *buildDecompressPanel(GtkStack *stack) {
  DecompressPanelWidgets *widgets = malloc(sizeof(DecompressPanelWidgets));

  GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(container, 20);
  gtk_widget_set_margin_end(container, 20);
  gtk_widget_set_margin_top(container, 20);

  // --- Header: back button + page icon + title ---
  GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *backButton = gtk_button_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(backButton),
                              "back-button");
  gtk_container_add(GTK_CONTAINER(backButton), loadPanelIcon("back.svg", 18));
  g_signal_connect(backButton, "clicked", G_CALLBACK(onBackToMenuClicked),
                   stack);

  GtkWidget *pageIcon = loadPanelIcon("decompress.svg", 22);
  GtkWidget *titleLabel = gtk_label_new("Descomprimir");
  gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel),
                              "panel-title");

  gtk_box_pack_start(GTK_BOX(headerBox), backButton, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(headerBox), pageIcon, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(headerBox), titleLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), headerBox, FALSE, FALSE, 8);

  // --- Archive file (.huff) ---
  GtkWidget *archiveLabel =
      gtk_label_new("Selecciona el archivo .huff a descomprimir:");
  gtk_widget_set_halign(archiveLabel, GTK_ALIGN_START);
  widgets->archiveFileChooser = gtk_file_chooser_button_new(
      "Elige un archivo", GTK_FILE_CHOOSER_ACTION_OPEN);

  // Restricts the file picker to .huff files, so the user can't
  // accidentally point the decompressor at an unrelated file.
  GtkFileFilter *huffFilter = gtk_file_filter_new();
  gtk_file_filter_set_name(huffFilter, "Archivos comprimidos (*.huff)");
  gtk_file_filter_add_pattern(huffFilter, "*.huff");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(widgets->archiveFileChooser),
                              huffFilter);

  gtk_box_pack_start(GTK_BOX(container), archiveLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->archiveFileChooser, FALSE,
                     FALSE, 0);

  // --- Method selector ---
  GtkWidget *methodLabel =
      gtk_label_new("Elige el método de descompresión a utilizar:");
  gtk_widget_set_halign(methodLabel, GTK_ALIGN_START);
  widgets->methodCombo = gtk_combo_box_text_new();
  for (int i = 0; i < DECOMPRESS_METHOD_COUNT; i++) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->methodCombo),
                                   DECOMPRESS_METHODS[i].label);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->methodCombo), 0);
  gtk_box_pack_start(GTK_BOX(container), methodLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->methodCombo, FALSE, FALSE, 0);

  // --- Destination folder + output folder name ---
  GtkWidget *destinationLabel =
      gtk_label_new("Selecciona dónde extraer los archivos:");
  gtk_widget_set_halign(destinationLabel, GTK_ALIGN_START);
  widgets->destinationFolderChooser = gtk_file_chooser_button_new(
      "Elige una carpeta", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_box_pack_start(GTK_BOX(container), destinationLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->destinationFolderChooser,
                     FALSE, FALSE, 0);

  GtkWidget *outputNameLabel =
      gtk_label_new("Nombre para la carpeta con los archivos extraídos:");
  gtk_widget_set_halign(outputNameLabel, GTK_ALIGN_START);
  widgets->outputNameEntry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "extracted");
  gtk_box_pack_start(GTK_BOX(container), outputNameLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->outputNameEntry, FALSE, FALSE,
                     0);

  // --- Run button + spinner ---
  GtkWidget *runBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  widgets->runButton = gtk_button_new_with_label("Descomprimir");
  gtk_style_context_add_class(gtk_widget_get_style_context(widgets->runButton),
                              "accent-button");
  widgets->spinner = gtk_spinner_new();
  gtk_box_pack_start(GTK_BOX(runBox), widgets->runButton, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(runBox), widgets->spinner, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), runBox, FALSE, FALSE, 10);

  g_signal_connect(widgets->runButton, "clicked",
                   G_CALLBACK(onDecompressButtonClicked), widgets);

  // --- Result area ---
  widgets->resultLabel = gtk_label_new("");
  gtk_label_set_line_wrap(GTK_LABEL(widgets->resultLabel), TRUE);
  gtk_widget_set_halign(widgets->resultLabel, GTK_ALIGN_START);
  gtk_label_set_selectable(GTK_LABEL(widgets->resultLabel), TRUE);

  GtkWidget *resultScroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(resultScroll, -1, 180);
  gtk_container_add(GTK_CONTAINER(resultScroll), widgets->resultLabel);
  gtk_box_pack_start(GTK_BOX(container), resultScroll, TRUE, TRUE, 0);

  g_signal_connect(container, "map", G_CALLBACK(onDecompressPanelMapped),
                   widgets);

  return container;
}
