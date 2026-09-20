#define _POSIX_C_SOURCE 200809L

#include "panel_compress.h"
#include "process_runner.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>

// Maps each combo box entry to the executable that implements it, so
// adding a fourth version later (if ever) means touching only this
// array, not the click handler's logic.
typedef struct
{
    const char *label;
    const char *executablePath;
} MethodOption;

static const MethodOption COMPRESS_METHODS[] = {
    {"Serial", "../serial/serial_compressor"},
    {"fork() + IPC", "../fork_ipc/fork_compressor"},
    {"pthread + memoria compartida", "../pthreads/pthread_compressor"},
};
#define COMPRESS_METHOD_COUNT 3

// Bundles every widget the run button's handler needs to read from or
// update, so a single pointer can be passed through g_object_set_data
// instead of relying on global variables.
typedef struct
{
    GtkWidget *inputFolderChooser;
    GtkWidget *methodCombo;
    GtkWidget *outputFolderChooser;
    GtkWidget *outputNameEntry;
    GtkWidget *runButton;
    GtkWidget *spinner;
    GtkWidget *resultLabel;
} CompressPanelWidgets;

// Carries data from the background thread back to the main thread.
// Allocated on the heap because the thread and the GTK idle callback
// run at different times — it must outlive the function that created it.
typedef struct
{
    CompressPanelWidgets *widgets;
    ProcessResult result;
} CompressResultMessage;

static char *buildOutputPath(const char *outputFolder, const char *enteredName)
{
    // Strips a trailing ".huff" the user may have typed manually, so
    // the extension is never doubled regardless of what they entered.
    size_t nameLength = strlen(enteredName);
    if (nameLength >= 5 && strcmp(enteredName + nameLength - 5, ".huff") == 0)
    {
        nameLength -= 5;
    }

    char *outputPath = malloc(strlen(outputFolder) + 1 + nameLength + 6);
    sprintf(outputPath, "%s/%.*s.huff", outputFolder, (int)nameLength, enteredName);
    return outputPath;
}

// Runs on the GTK main thread (scheduled via g_idle_add), so this is
// the only place in this panel allowed to touch widgets after the
// background thread starts. Returning G_SOURCE_REMOVE tells GLib this
// idle callback should not be called again.
static gboolean applyCompressResultOnMainThread(gpointer userData)
{
    CompressResultMessage *message = (CompressResultMessage *)userData;
    CompressPanelWidgets *widgets = message->widgets;

    gtk_spinner_stop(GTK_SPINNER(widgets->spinner));
    gtk_widget_set_sensitive(widgets->runButton, TRUE);

    if (message->result.wasSuccessful)
    {
        char labelText[2200];
        snprintf(labelText, sizeof(labelText), "Compresion exitosa (%.3f s)\n\n%s",
                 message->result.elapsedSeconds, message->result.capturedOutputTail);
        gtk_label_set_text(GTK_LABEL(widgets->resultLabel), labelText);
        gtk_style_context_remove_class(gtk_widget_get_style_context(widgets->resultLabel), "status-error");
        gtk_style_context_add_class(gtk_widget_get_style_context(widgets->resultLabel), "status-success");
    }
    else
    {
        char labelText[2200];
        snprintf(labelText, sizeof(labelText), "La compresion fallo (%.3f s)\n\n%s",
                 message->result.elapsedSeconds, message->result.capturedOutputTail);
        gtk_label_set_text(GTK_LABEL(widgets->resultLabel), labelText);
        gtk_style_context_remove_class(gtk_widget_get_style_context(widgets->resultLabel), "status-success");
        gtk_style_context_add_class(gtk_widget_get_style_context(widgets->resultLabel), "status-error");
    }

    free(message);
    return G_SOURCE_REMOVE;
}

// Runs entirely off the GTK main thread: process_runner's fork/exec/wait
// is safe here since it never touches a widget directly. Only the
// g_idle_add call at the end crosses back into GTK-safe territory.
static void *runCompressionInBackground(void *argument)
{
    CompressResultMessage *message = (CompressResultMessage *)argument;
    CompressPanelWidgets *widgets = message->widgets;

    char *inputDirectory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->inputFolderChooser));
    char *outputFolder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
    const char *enteredName = gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));
    char *outputPath = buildOutputPath(outputFolder, enteredName);

    int methodIndex = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->methodCombo));
    const char *executablePath = COMPRESS_METHODS[methodIndex].executablePath;

    char *argv[] = {(char *)executablePath, inputDirectory, outputPath, NULL};
    message->result = runProcessAndMeasureTime(executablePath, argv);

    g_free(inputDirectory);
    g_free(outputFolder);
    free(outputPath);

    g_idle_add(applyCompressResultOnMainThread, message);
    return NULL;
}

static void onCompressButtonClicked(GtkButton *button, gpointer userData)
{
    (void)button;
    CompressPanelWidgets *widgets = (CompressPanelWidgets *)userData;

    char *inputDirectory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->inputFolderChooser));
    char *outputFolder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widgets->outputFolderChooser));
    const char *enteredName = gtk_entry_get_text(GTK_ENTRY(widgets->outputNameEntry));

    if (inputDirectory == NULL || outputFolder == NULL || strlen(enteredName) == 0)
    {
        gtk_label_set_text(GTK_LABEL(widgets->resultLabel),
                           "Selecciona la carpeta de entrada, la carpeta destino, y un nombre para el archivo.");
        g_free(inputDirectory);
        g_free(outputFolder);
        return;
    }
    g_free(inputDirectory);
    g_free(outputFolder);

    gtk_widget_set_sensitive(widgets->runButton, FALSE);
    gtk_spinner_start(GTK_SPINNER(widgets->spinner));
    gtk_label_set_text(GTK_LABEL(widgets->resultLabel), "Comprimiendo...");

    CompressResultMessage *message = malloc(sizeof(CompressResultMessage));
    message->widgets = widgets;

    // Detached: nothing needs to pthread_join this thread, since the
    // g_idle_add call at its end is how it reports back — there is no
    // result to collect by joining.
    pthread_t backgroundThread;
    pthread_create(&backgroundThread, NULL, runCompressionInBackground, message);
    pthread_detach(backgroundThread);
}

static void onBackToMenuClicked(GtkButton *button, gpointer userData)
{
    (void)button;
    gtk_stack_set_visible_child_name(GTK_STACK(userData), "menu");
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

GtkWidget *buildCompressPanel(GtkStack *stack)
{
    CompressPanelWidgets *widgets = malloc(sizeof(CompressPanelWidgets));

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(container, 20);
    gtk_widget_set_margin_end(container, 20);
    gtk_widget_set_margin_top(container, 20);

    // --- Header: back button + title ---
    GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *backButton = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(backButton), "back-button");
    gtk_container_add(GTK_CONTAINER(backButton), loadPanelIcon("back.svg", 18));
    g_signal_connect(backButton, "clicked", G_CALLBACK(onBackToMenuClicked), stack);

    GtkWidget *titleLabel = gtk_label_new("Comprimir");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "panel-title");

    gtk_box_pack_start(GTK_BOX(headerBox), backButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerBox), titleLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), headerBox, FALSE, FALSE, 8);

    // --- Input folder ---
    GtkWidget *inputLabel = gtk_label_new("Carpeta a comprimir:");
    gtk_widget_set_halign(inputLabel, GTK_ALIGN_START);
    widgets->inputFolderChooser = gtk_file_chooser_button_new("Elige una carpeta",
                                                              GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_box_pack_start(GTK_BOX(container), inputLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->inputFolderChooser, FALSE, FALSE, 0);

    // --- Method selector ---
    GtkWidget *methodLabel = gtk_label_new("Metodo:");
    gtk_widget_set_halign(methodLabel, GTK_ALIGN_START);
    widgets->methodCombo = gtk_combo_box_text_new();
    for (int i = 0; i < COMPRESS_METHOD_COUNT; i++)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widgets->methodCombo), COMPRESS_METHODS[i].label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->methodCombo), 0);
    gtk_box_pack_start(GTK_BOX(container), methodLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->methodCombo, FALSE, FALSE, 0);

    // --- Output folder + filename ---
    GtkWidget* outputFolderLabel = gtk_label_new("Carpeta donde guardar el resultado:");
    gtk_widget_set_halign(outputFolderLabel, GTK_ALIGN_START);
    widgets->outputFolderChooser = gtk_file_chooser_button_new("Elige una carpeta",
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_box_pack_start(GTK_BOX(container), outputFolderLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->outputFolderChooser, FALSE, FALSE, 0);

    GtkWidget* outputNameLabel = gtk_label_new("Nombre del archivo (sin extension):");
    gtk_widget_set_halign(outputNameLabel, GTK_ALIGN_START);
    widgets->outputNameEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widgets->outputNameEntry), "output");
    gtk_box_pack_start(GTK_BOX(container), outputNameLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), widgets->outputNameEntry, FALSE, FALSE, 0);

    // --- Run button + spinner ---
    GtkWidget *runBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    widgets->runButton = gtk_button_new_with_label("Comprimir");
    gtk_style_context_add_class(gtk_widget_get_style_context(widgets->runButton), "accent-button");
    widgets->spinner = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(runBox), widgets->runButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(runBox), widgets->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(container), runBox, FALSE, FALSE, 10);

    g_signal_connect(widgets->runButton, "clicked", G_CALLBACK(onCompressButtonClicked), widgets);

    // --- Result area ---
    widgets->resultLabel = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(widgets->resultLabel), TRUE);
    gtk_widget_set_halign(widgets->resultLabel, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(widgets->resultLabel), TRUE);

    GtkWidget *resultScroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(resultScroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(resultScroll), widgets->resultLabel);
    gtk_box_pack_start(GTK_BOX(container), resultScroll, TRUE, TRUE, 0);

    return container;
}
