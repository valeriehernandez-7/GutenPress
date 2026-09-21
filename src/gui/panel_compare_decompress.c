#define _POSIX_C_SOURCE 200809L

#include "panel_compare_decompress.h"
#include "process_runner.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  const char *label;
  const char *executablePath;
  const char *folderSuffix; // e.g. "serial", used to build "<name>_serial" as
                            // the extraction folder
} MethodOption;

static const MethodOption DECOMPRESS_METHODS[] = {
    {"Secuencial (serial)", "../serial/serial_decompressor", "serial"},
    {"Paralelo por procesos (fork + IPC)", "../fork_ipc/fork_decompressor",
     "fork"},
    {"Paralelo por hilos (pthreads)", "../pthreads/pthread_decompressor",
     "pthread"},
};
#define METHOD_COUNT 3

typedef struct {
  bool wasSuccessful;
  double elapsedSeconds;
  int filesExtracted;
  int filesTotal;
  int md5Verified;
  double healthPercent;
  double speedupPercent; // 0 for serial itself (it IS the baseline)
} MethodResult;

typedef struct {
  GtkWidget *archiveFileChooser;
  GtkWidget *outputFolderChooser;
  GtkWidget *outputNameEntry;
  GtkWidget *runButton;
  GtkWidget *spinner;
  GtkWidget *statusLabel;
  GtkWidget *conclusionLabel;
  GtkListStore *resultsStore;
  GtkWidget *resultsView;
} ComparePanelWidgets;

typedef struct {
  ComparePanelWidgets *widgets;
  MethodResult results[METHOD_COUNT];
  bool allSucceeded;
} CompareResultMessage;

// Pulls "Files extracted: 100/100", "MD5 verified: 100/100" and
// "Compression health: 100.00%" out of the same text every decompressor
// already prints, the same way parseCompressorOutput reuses the
// compressors' own summary lines instead of a second data format.
static void parseDecompressorOutput(const char *output, MethodResult *result) {
  result->filesExtracted = 0;
  result->filesTotal = 0;
  result->md5Verified = 0;
  result->healthPercent = 0.0;

  const char *extractedLine = strstr(output, "Files extracted:");
  if (extractedLine != NULL) {
    sscanf(extractedLine, "Files extracted: %d/%d", &result->filesExtracted,
           &result->filesTotal);
  }

  const char *verifiedLine = strstr(output, "MD5 verified:");
  if (verifiedLine != NULL) {
    sscanf(verifiedLine, "MD5 verified: %d/", &result->md5Verified);
  }

  const char *healthLine = strstr(output, "Compression health:");
  if (healthLine != NULL) {
    sscanf(healthLine, "Compression health: %lf", &result->healthPercent);
  }
}

static char *buildOutputBaseFolderPath(const char *baseFolder,
                                       const char *baseName) {
  char *folderPath = malloc(strlen(baseFolder) + 1 + strlen(baseName) + 1);
  sprintf(folderPath, "%s/%s", baseFolder, baseName);
  return folderPath;
}

// Builds "<baseName>_<methodSuffix>" as the extraction folder inside
// the run's own base folder, mirroring the "<name>_<method>.huff"
// pattern used for compression, so a viewer sees the same naming
// convention regardless of which panel produced a given result.
static void buildExtractionFolderPath(char *outPath, size_t outPathCapacity,
                                      const char *baseFolderPath,
                                      const char *baseName,
                                      const char *methodSuffix) {
  snprintf(outPath, outPathCapacity, "%s/%s_%s", baseFolderPath, baseName,
           methodSuffix);
}

static gboolean applyCompareResultOnMainThread(gpointer userData) {
  CompareResultMessage *message = (CompareResultMessage *)userData;
  ComparePanelWidgets *widgets = message->widgets;

  gtk_spinner_stop(GTK_SPINNER(widgets->spinner));
  gtk_widget_set_sensitive(widgets->runButton, TRUE);

  gtk_list_store_clear(widgets->resultsStore);

  int fastestIndex = 0;
  bool allHealthMatches = true;

  for (int i = 0; i < METHOD_COUNT; i++) {
    MethodResult *result = &message->results[i];
    GtkTreeIter iter;
    gtk_list_store_append(widgets->resultsStore, &iter);

    char timeText[32];
    snprintf(timeText, sizeof(timeText), "%.3f s", result->elapsedSeconds);

    char speedupText[32];
    if (i == 0) {
      snprintf(speedupText, sizeof(speedupText), "(base)");
    } else {
      snprintf(speedupText, sizeof(speedupText), "%.1f%%",
               result->speedupPercent);
    }

    char extractedText[32];
    snprintf(extractedText, sizeof(extractedText), "%d/%d",
             result->filesExtracted, result->filesTotal);

    char verifiedText[32];
    snprintf(verifiedText, sizeof(verifiedText), "%d/%d", result->md5Verified,
             result->filesTotal);

    char healthText[16];
    snprintf(healthText, sizeof(healthText), "%.2f%%", result->healthPercent);

    gtk_list_store_set(
        widgets->resultsStore, &iter, 0, DECOMPRESS_METHODS[i].label, 1,
        result->wasSuccessful ? "OK" : "FALLO", 2, timeText, 3, speedupText, 4,
        extractedText, 5, verifiedText, 6, healthText, -1);

    if (result->wasSuccessful &&
        result->elapsedSeconds <
            message->results[fastestIndex].elapsedSeconds) {
      fastestIndex = i;
    }
    if (result->healthPercent < 100.0) {
      allHealthMatches = false;
    }
  }

  gtk_label_set_text(
      GTK_LABEL(widgets->statusLabel),
      message->allSucceeded
          ? "Análisis completado."
          : "Análisis completado con errores en al menos un método.");

  char conclusionText[512];
  snprintf(
      conclusionText, sizeof(conclusionText),
      "Método más rápido: %s, con %.3f s (%.1f%% más rápido que la versión "
      "secuencial).\n"
      "%s",
      DECOMPRESS_METHODS[fastestIndex].label,
      message->results[fastestIndex].elapsedSeconds,
      message->results[fastestIndex].speedupPercent,
      allHealthMatches
          ? "Los 3 métodos verificaron el 100% de las firmas MD5, confirmando "
            "una descompresión correcta en cada estrategia de paralelismo."
          : "Al menos un método no alcanzó 100% de salud de compresión — "
            "revisa la tabla para identificar cual.");
  gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), conclusionText);

  free(message);
  return G_SOURCE_REMOVE;
}

static void *runComparisonInBackground(void *argument) {
  CompareResultMessage *message = (CompareResultMessage *)argument;
  ComparePanelWidgets *widgets = message->widgets;

  char *archivePath = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  char *outputBaseFolder = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->outputFolderChooser));
  const char *baseName =
      gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));
  char *baseFolderPath = buildOutputBaseFolderPath(outputBaseFolder, baseName);

  mkdir(baseFolderPath, 0755); // ignore EEXIST; each run below reports its own
                               // failure if the folder is unusable

  message->allSucceeded = true;
  double serialElapsedSeconds = 0.0;

  for (int i = 0; i < METHOD_COUNT; i++) {
    char extractionFolder[4096];
    buildExtractionFolderPath(extractionFolder, sizeof(extractionFolder),
                              baseFolderPath, baseName,
                              DECOMPRESS_METHODS[i].folderSuffix);

    char *argv[] = {(char *)DECOMPRESS_METHODS[i].executablePath, archivePath,
                    extractionFolder, NULL};
    ProcessResult processResult =
        runProcessAndMeasureTime(DECOMPRESS_METHODS[i].executablePath, argv);

    MethodResult *result = &message->results[i];
    result->wasSuccessful = processResult.wasSuccessful;
    result->elapsedSeconds = processResult.elapsedSeconds;
    parseDecompressorOutput(processResult.capturedOutputTail, result);

    if (i == 0) {
      serialElapsedSeconds = processResult.elapsedSeconds;
      result->speedupPercent = 0.0;
    } else if (serialElapsedSeconds > 0.0) {
      result->speedupPercent =
          ((serialElapsedSeconds - processResult.elapsedSeconds) /
           serialElapsedSeconds) *
          100.0;
    } else {
      result->speedupPercent = 0.0;
    }

    if (!processResult.wasSuccessful) {
      message->allSucceeded = false;
    }
  }

  g_free(archivePath);
  g_free(outputBaseFolder);
  free(baseFolderPath);

  g_idle_add(applyCompareResultOnMainThread, message);
  return NULL;
}

static void onRunButtonClicked(GtkButton *button, gpointer userData) {
  (void)button;
  ComparePanelWidgets *widgets = (ComparePanelWidgets *)userData;

  char *archivePath = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  char *outputBaseFolder = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(widgets->outputFolderChooser));
  const char *baseName =
      gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));

  if (archivePath == NULL || outputBaseFolder == NULL ||
      strlen(baseName) == 0) {
    gtk_label_set_text(
        GTK_LABEL(widgets->statusLabel),
        "Selecciona el archivo .huff, la carpeta destino, y un nombre base.");
    g_free(archivePath);
    g_free(outputBaseFolder);
    return;
  }
  g_free(archivePath);
  g_free(outputBaseFolder);

  gtk_widget_set_sensitive(widgets->runButton, FALSE);
  gtk_spinner_start(GTK_SPINNER(widgets->spinner));
  gtk_label_set_text(GTK_LABEL(widgets->statusLabel),
                     "Ejecutando los 3 métodos secuencialmente, esto puede "
                     "tardar varios segundos...");
  gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), "");
  gtk_list_store_clear(widgets->resultsStore);

  CompareResultMessage *message = malloc(sizeof(CompareResultMessage));
  message->widgets = widgets;

  pthread_t backgroundThread;
  pthread_create(&backgroundThread, NULL, runComparisonInBackground, message);
  pthread_detach(backgroundThread);
}

static void onBackToMenuClicked(GtkButton *button, gpointer userData) {
  (void)button;
  gtk_stack_set_visible_child_name(GTK_STACK(userData), "menu");
}

static void onComparePanelMapped(GtkWidget *widget, gpointer userData) {
  (void)widget;
  ComparePanelWidgets *widgets = (ComparePanelWidgets *)userData;

  gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(widgets->archiveFileChooser));
  gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
  gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "comparacion");
  gtk_label_set_text(GTK_LABEL(widgets->statusLabel), "");
  gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), "");
  gtk_list_store_clear(widgets->resultsStore);
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

static void addTextColumn(GtkTreeView *treeView, const char *title,
                          int columnIndex) {
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
      title, renderer, "text", columnIndex, NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(treeView, column);
}

GtkWidget *buildCompareDecompressPanel(GtkStack *stack) {
  ComparePanelWidgets *widgets = malloc(sizeof(ComparePanelWidgets));

  GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(container, 20);
  gtk_widget_set_margin_end(container, 20);
  gtk_widget_set_margin_top(container, 20);
  gtk_widget_set_margin_bottom(container, 20);

  // --- Header ---
  GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *backButton = gtk_button_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(backButton),
                              "back-button");
  gtk_container_add(GTK_CONTAINER(backButton), loadPanelIcon("back.svg", 18));
  g_signal_connect(backButton, "clicked", G_CALLBACK(onBackToMenuClicked),
                   stack);

  GtkWidget *pageIcon = loadPanelIcon("scan-decompress.svg", 22);
  GtkWidget *titleLabel = gtk_label_new("Analizar Descompresión");
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

  GtkFileFilter *huffFilter = gtk_file_filter_new();
  gtk_file_filter_set_name(huffFilter, "Archivos comprimidos (*.huff)");
  gtk_file_filter_add_pattern(huffFilter, "*.huff");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(widgets->archiveFileChooser),
                              huffFilter);

  gtk_box_pack_start(GTK_BOX(container), archiveLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->archiveFileChooser, FALSE,
                     FALSE, 0);

  // --- Output base folder + base name ---
  GtkWidget *outputFolderLabel =
      gtk_label_new("Selecciona la carpeta base donde extraer los resultados:");
  gtk_widget_set_halign(outputFolderLabel, GTK_ALIGN_START);
  widgets->outputFolderChooser = gtk_file_chooser_button_new(
      "Elige una carpeta", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_box_pack_start(GTK_BOX(container), outputFolderLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->outputFolderChooser, FALSE,
                     FALSE, 0);

  GtkWidget *outputNameLabel = gtk_label_new("Nombre base para las carpetas:");
  gtk_widget_set_halign(outputNameLabel, GTK_ALIGN_START);
  widgets->outputNameEntry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "comparacion");
  gtk_box_pack_start(GTK_BOX(container), outputNameLabel, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), widgets->outputNameEntry, FALSE, FALSE,
                     0);

  GtkWidget *outputNameHint = gtk_label_new(
      "Ej.: comparacion_serial/, comparacion_fork/, comparacion_pthread/");
  gtk_widget_set_halign(outputNameHint, GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(outputNameHint),
                              "card-subtitle");
  gtk_box_pack_start(GTK_BOX(container), outputNameHint, FALSE, FALSE, 0);

  // --- Run button + spinner ---
  GtkWidget *runBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  widgets->runButton = gtk_button_new_with_label("Ejecutar análisis");
  gtk_style_context_add_class(gtk_widget_get_style_context(widgets->runButton),
                              "accent-button");
  widgets->spinner = gtk_spinner_new();
  gtk_box_pack_start(GTK_BOX(runBox), widgets->runButton, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(runBox), widgets->spinner, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(container), runBox, FALSE, FALSE, 10);

  g_signal_connect(widgets->runButton, "clicked",
                   G_CALLBACK(onRunButtonClicked), widgets);

  widgets->statusLabel = gtk_label_new("");
  gtk_widget_set_halign(widgets->statusLabel, GTK_ALIGN_START);
  gtk_box_pack_start(GTK_BOX(container), widgets->statusLabel, FALSE, FALSE, 0);

  // --- Results table ---
  // Columns: Metodo, Estado, Tiempo, Aceleracion, Extraidos, MD5 verificados,
  // Salud
  widgets->resultsStore = gtk_list_store_new(
      7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  widgets->resultsView =
      gtk_tree_view_new_with_model(GTK_TREE_MODEL(widgets->resultsStore));
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Método", 0);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Estado", 1);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Tiempo", 2);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Aceleración", 3);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Archivos extraídos", 4);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "MD5 verificados", 5);
  addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Salud", 6);

  GtkWidget *tableScroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(tableScroll, -1, 140);
  gtk_container_add(GTK_CONTAINER(tableScroll), widgets->resultsView);
  gtk_box_pack_start(GTK_BOX(container), tableScroll, TRUE, TRUE, 0);

  // --- Conclusion ---
  widgets->conclusionLabel = gtk_label_new("");
  gtk_label_set_line_wrap(GTK_LABEL(widgets->conclusionLabel), TRUE);
  gtk_widget_set_halign(widgets->conclusionLabel, GTK_ALIGN_START);
  gtk_label_set_selectable(GTK_LABEL(widgets->conclusionLabel), TRUE);
  gtk_box_pack_start(GTK_BOX(container), widgets->conclusionLabel, FALSE, FALSE,
                     8);

  g_signal_connect(container, "map", G_CALLBACK(onComparePanelMapped), widgets);

  return container;
}
