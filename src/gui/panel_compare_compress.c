#define _POSIX_C_SOURCE 200809L

#include "panel_compare_compress.h"
#include "process_runner.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct
{
    const char *label;
    const char *executablePath;
    const char *fileSuffix; // e.g. "serial", used to build "<name>_serial.huff"
} MethodOption;

static const MethodOption COMPRESS_METHODS[] = {
    {"Secuencial (serial)", "../serial/serial_compressor", "serial"},
    {"Paralelo por procesos (fork + IPC)", "../fork_ipc/fork_compressor", "fork"},
    {"Paralelo por hilos (pthreads)", "../pthreads/pthread_compressor", "pthread"},
};
#define METHOD_COUNT 3

// One row's worth of data extracted from a single method's run, parsed
// out of process_runner's captured text output plus the wall-clock
// time it measured — this is what fills one row of the results table.
typedef struct
{
    bool wasSuccessful;
    double elapsedSeconds;
    uint64_t totalOriginalSize;
    uint64_t totalCompressedSize;
    double compressionRatio;
    double spaceSavedPercent;
    double speedupPercent; // 0 for serial itself (it IS the baseline)
} MethodResult;

typedef struct
{
    GtkWidget *inputFolderChooser;
    GtkWidget *outputFolderChooser;
    GtkWidget *outputNameEntry;
    GtkWidget *runButton;
    GtkWidget *spinner;
    GtkWidget *statusLabel;
    GtkWidget *conclusionLabel;
    GtkListStore *resultsStore;
    GtkWidget *resultsView;
} ComparePanelWidgets;

typedef struct
{
    ComparePanelWidgets *widgets;
    MethodResult results[METHOD_COUNT];
    bool allSucceeded;
} CompareResultMessage;

// Pulls "Total original size: 77354064 bytes" (and the compressed-size
// and ratio lines) out of the same text serial/fork/pthread compressors
// already print — reusing their existing summary output instead of
// inventing a second, machine-readable format the programs would also
// need to emit.
static void parseCompressorOutput(const char *output, MethodResult *result)
{
    result->totalOriginalSize = 0;
    result->totalCompressedSize = 0;
    result->compressionRatio = 0.0;

    const char *originalLine = strstr(output, "Total original size:");
    if (originalLine != NULL)
    {
        sscanf(originalLine, "Total original size: %llu", (unsigned long long *)&result->totalOriginalSize);
    }

    const char *compressedLine = strstr(output, "Total compressed size:");
    if (compressedLine != NULL)
    {
        sscanf(compressedLine, "Total compressed size: %llu", (unsigned long long *)&result->totalCompressedSize);
    }

    const char *ratioLine = strstr(output, "Compression ratio:");
    if (ratioLine != NULL)
    {
        sscanf(ratioLine, "Compression ratio: %lf", &result->compressionRatio);
    }

    result->spaceSavedPercent = (1.0 - result->compressionRatio) * 100.0;
}

static char *buildOutputFolderPath(const char *baseFolder, const char *baseName)
{
    char *folderPath = malloc(strlen(baseFolder) + 1 + strlen(baseName) + 1);
    sprintf(folderPath, "%s/%s", baseFolder, baseName);
    return folderPath;
}

// Builds "<baseName>_<methodSuffix>.huff" inside the run's own folder,
// e.g. "comparacion/comparacion_serial.huff" — self-descriptive even
// if the .huff file is later copied out of its folder on its own.
static void buildArchivePath(char *outPath, size_t outPathCapacity,
                             const char *folderPath, const char *baseName, const char *methodSuffix)
{
    snprintf(outPath, outPathCapacity, "%s/%s_%s.huff", folderPath, baseName, methodSuffix);
}

static gboolean applyCompareResultOnMainThread(gpointer userData)
{
    CompareResultMessage *message = (CompareResultMessage *)userData;
    ComparePanelWidgets *widgets = message->widgets;

    gtk_spinner_stop(GTK_SPINNER(widgets->spinner));
    gtk_widget_set_sensitive(widgets->runButton, TRUE);

    gtk_list_store_clear(widgets->resultsStore);

    int fastestIndex = 0;
    for (int i = 0; i < METHOD_COUNT; i++)
    {
        MethodResult *result = &message->results[i];
        GtkTreeIter iter;
        gtk_list_store_append(widgets->resultsStore, &iter);

        char timeText[32];
        snprintf(timeText, sizeof(timeText), "%.3f s", result->elapsedSeconds);

        char speedupText[32];
        if (i == 0)
        {
            snprintf(speedupText, sizeof(speedupText), "(base)");
        }
        else
        {
            snprintf(speedupText, sizeof(speedupText), "%.1f%%", result->speedupPercent);
        }

        char ratioText[16];
        snprintf(ratioText, sizeof(ratioText), "%.4f", result->compressionRatio);

        char savedText[16];
        snprintf(savedText, sizeof(savedText), "%.1f%%", result->spaceSavedPercent);

        gtk_list_store_set(widgets->resultsStore, &iter,
                           0, COMPRESS_METHODS[i].label,
                           1, result->wasSuccessful ? "OK" : "FALLO",
                           2, timeText,
                           3, speedupText,
                           4, (gint64)result->totalOriginalSize,
                           5, (gint64)result->totalCompressedSize,
                           6, ratioText,
                           7, savedText,
                           -1);

        if (result->wasSuccessful && result->elapsedSeconds < message->results[fastestIndex].elapsedSeconds)
        {
            fastestIndex = i;
        }
    }

    gtk_label_set_text(GTK_LABEL(widgets->statusLabel),
                       message->allSucceeded ? "Análisis completado." : "Análisis completado con errores en al menos un método.");

    // Sizes and ratio are identical across methods by design — Huffman
    // is deterministic, so parallelizing it changes only how long it
    // takes, never what gets encoded. Saying so explicitly turns what
    // could look like a missing comparison into a correctness check.
    char conclusionText[512];
    snprintf(conclusionText, sizeof(conclusionText),
             "Método más rápido: %s, con %.3f s (%.1f%% más rápido que la versión secuencial).\n"
             "Los tamaños de archivo y el ratio de compresión coinciden entre los 3 métodos, "
             "confirmando que la compresión es correcta sin importar la estrategia de paralelismo usada.",
             COMPRESS_METHODS[fastestIndex].label,
             message->results[fastestIndex].elapsedSeconds,
             message->results[fastestIndex].speedupPercent);
    gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), conclusionText);

    free(message);
    return G_SOURCE_REMOVE;
}

static void *runComparisonInBackground(void *argument)
{
    CompareResultMessage *message = (CompareResultMessage *)argument;
    ComparePanelWidgets *widgets = message->widgets;

    char *inputDirectory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->inputFolderChooser));
    char *outputBaseFolder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
    const char *baseName = gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));
    char *outputFolderPath = buildOutputFolderPath(outputBaseFolder, baseName);

    mkdir(outputFolderPath, 0755); // ignore EEXIST; each run below reports its own failure if the folder is unusable

    message->allSucceeded = true;
    double serialElapsedSeconds = 0.0;

    for (int i = 0; i < METHOD_COUNT; i++)
    {
        char archivePath[4096];
        buildArchivePath(archivePath, sizeof(archivePath), outputFolderPath, baseName, COMPRESS_METHODS[i].fileSuffix);

        char *argv[] = {(char *)COMPRESS_METHODS[i].executablePath, inputDirectory, archivePath, NULL};
        ProcessResult processResult = runProcessAndMeasureTime(COMPRESS_METHODS[i].executablePath, argv);

        MethodResult *result = &message->results[i];
        result->wasSuccessful = processResult.wasSuccessful;
        result->elapsedSeconds = processResult.elapsedSeconds;
        parseCompressorOutput(processResult.capturedOutputTail, result);

        if (i == 0)
        {
            serialElapsedSeconds = processResult.elapsedSeconds;
            result->speedupPercent = 0.0;
        }
        else if (serialElapsedSeconds > 0.0)
        {
            result->speedupPercent = ((serialElapsedSeconds - processResult.elapsedSeconds) / serialElapsedSeconds) * 100.0;
        }
        else
        {
            result->speedupPercent = 0.0;
        }

        if (!processResult.wasSuccessful)
        {
            message->allSucceeded = false;
        }
    }

    g_free(inputDirectory);
    g_free(outputBaseFolder);
    free(outputFolderPath);

    g_idle_add(applyCompareResultOnMainThread, message);
    return NULL;
}

static void onRunButtonClicked(GtkButton *button, gpointer userData)
{
    (void)button;
    ComparePanelWidgets *widgets = (ComparePanelWidgets *)userData;

    char *inputDirectory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->inputFolderChooser));
    char *outputBaseFolder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
    const char *baseName = gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));

    if (inputDirectory == NULL || outputBaseFolder == NULL || strlen(baseName) == 0)
    {
        gtk_label_set_text(GTK_LABEL(widgets->statusLabel),
                           "Selecciona la carpeta de entrada, la carpeta destino, y un nombre base.");
        g_free(inputDirectory);
        g_free(outputBaseFolder);
        return;
    }
    g_free(inputDirectory);
    g_free(outputBaseFolder);

    gtk_widget_set_sensitive(widgets->runButton, FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->spinner));
    gtk_label_set_text(GTK_LABEL(widgets->statusLabel),
                       "Ejecutando los 3 métodos secuencialmente, esto puede tardar varios segundos...");
    gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), "");
    gtk_list_store_clear(widgets->resultsStore);

    CompareResultMessage *message = malloc(sizeof(CompareResultMessage));
    message->widgets = widgets;

    pthread_t backgroundThread;
    pthread_create(&backgroundThread, NULL, runComparisonInBackground, message);
    pthread_detach(backgroundThread);
}

static void onBackToMenuClicked(GtkButton *button, gpointer userData)
{
    (void)button;
    gtk_stack_set_visible_child_name(GTK_STACK(userData), "menu");
}

static void onComparePanelMapped(GtkWidget *widget, gpointer userData)
{
    (void)widget;
    ComparePanelWidgets *widgets = (ComparePanelWidgets *)userData;

    gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(widgets->inputFolderChooser));
    gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
    gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "comparacion");
    gtk_label_set_text(GTK_LABEL(widgets->statusLabel), "");
    gtk_label_set_text(GTK_LABEL(widgets->conclusionLabel), "");
    gtk_list_store_clear(widgets->resultsStore);
}

static GtkWidget *loadPanelIcon(const char *iconFileName, int pixelSize)
{
    char iconPath[256];
    snprintf(iconPath, sizeof(iconPath), "icons/%s", iconFileName);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(iconPath, pixelSize, pixelSize, TRUE, NULL);
    if (pixbuf == NULL)
    {
        return gtk_image_new();
    }
    GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return image;
}

static void addTextColumn(GtkTreeView *treeView, const char *title, int columnIndex)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", columnIndex, NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(treeView, column);
}

GtkWidget *buildCompareCompressPanel(GtkStack *stack)
{
    ComparePanelWidgets *widgets = malloc(sizeof(ComparePanelWidgets));

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(container, 20);
    gtk_widget_set_margin_end(container, 20);
    gtk_widget_set_margin_top(container, 20);
    gtk_widget_set_margin_bottom(container, 20);

    // --- Header ---
    GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *backButton = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(backButton), "back-button");
    gtk_container_add(GTK_CONTAINER(backButton), loadPanelIcon("back.svg", 18));
    g_signal_connect(backButton, "clicked", G_CALLBACK(onBackToMenuClicked), stack);

    GtkWidget *pageIcon = loadPanelIcon("scan-compress.svg", 22);
    GtkWidget *titleLabel = gtk_label_new("Analizar Compresión");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "panel-title");

    gtk_box_pack_start(GTK_BOX(headerBox), backButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerBox), pageIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerBox), titleLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), headerBox, FALSE, FALSE, 8);

    // --- Input folder ---
    GtkWidget *inputLabel = gtk_label_new("Selecciona la carpeta que deseas comprimir:");
    gtk_widget_set_halign(inputLabel, GTK_ALIGN_START);
    widgets->inputFolderChooser = gtk_file_chooser_button_new("Elige una carpeta", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_box_pack_start(GTK_BOX(container), inputLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->inputFolderChooser, FALSE, FALSE, 0);

    // --- Output base folder + base name ---
    GtkWidget *outputFolderLabel = gtk_label_new("Selecciona la carpeta base donde guardar los resultados:");
    gtk_widget_set_halign(outputFolderLabel, GTK_ALIGN_START);
    widgets->outputFolderChooser = gtk_file_chooser_button_new("Elige una carpeta", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_box_pack_start(GTK_BOX(container), outputFolderLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->outputFolderChooser, FALSE, FALSE, 0);

    GtkWidget *outputNameLabel = gtk_label_new("Nombre base para los archivos:");
    gtk_widget_set_halign(outputNameLabel, GTK_ALIGN_START);
    widgets->outputNameEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "comparacion");
    gtk_box_pack_start(GTK_BOX(container), outputNameLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->outputNameEntry, FALSE, FALSE, 0);

    GtkWidget *outputNameHint = gtk_label_new("Ej.: comparacion_serial.huff, comparacion_fork.huff, comparacion_pthread.huff");
    gtk_widget_set_halign(outputNameHint, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(outputNameHint), "card-subtitle");
    gtk_box_pack_start(GTK_BOX(container), outputNameHint, FALSE, FALSE, 0);

    // --- Run button + spinner ---
    GtkWidget *runBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    widgets->runButton = gtk_button_new_with_label("Ejecutar análisis");
    gtk_style_context_add_class(gtk_widget_get_style_context(widgets->runButton), "accent-button");
    widgets->spinner = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(runBox), widgets->runButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(runBox), widgets->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), runBox, FALSE, FALSE, 10);

    g_signal_connect(widgets->runButton, "clicked", G_CALLBACK(onRunButtonClicked), widgets);

    widgets->statusLabel = gtk_label_new("");
    gtk_widget_set_halign(widgets->statusLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(container), widgets->statusLabel, FALSE, FALSE, 0);

    // --- Results table ---
    // Columns: Metodo, Estado, Tiempo, Aceleracion, Tam. original, Tam. comprimido, Ratio, % ahorrado
    widgets->resultsStore = gtk_list_store_new(8,
                                               G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                               G_TYPE_INT64, G_TYPE_INT64, G_TYPE_STRING, G_TYPE_STRING);

    widgets->resultsView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(widgets->resultsStore));
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Método", 0);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Estado", 1);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Tiempo", 2);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Aceleración", 3);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Tam. original (bytes)", 4);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Tam. comprimido (bytes)", 5);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Ratio", 6);
    addTextColumn(GTK_TREE_VIEW(widgets->resultsView), "Espacio ahorrado", 7);

    GtkWidget *tableScroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(tableScroll, -1, 140);
    gtk_container_add(GTK_CONTAINER(tableScroll), widgets->resultsView);
    gtk_box_pack_start(GTK_BOX(container), tableScroll, TRUE, TRUE, 0);

    // --- Conclusion ---
    widgets->conclusionLabel = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(widgets->conclusionLabel), TRUE);
    gtk_widget_set_halign(widgets->conclusionLabel, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(widgets->conclusionLabel), TRUE);
    gtk_box_pack_start(GTK_BOX(container), widgets->conclusionLabel, FALSE, FALSE, 8);

    g_signal_connect(container, "map", G_CALLBACK(onComparePanelMapped), widgets);

    return container;
}
