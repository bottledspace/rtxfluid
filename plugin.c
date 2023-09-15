#include "plugin.h"
#include "database.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


struct Plugin *plugins = NULL;

// This internal method unloads WITHOUT free-ing. This is internal since it is
// only useful to us in P_reload for saving a malloc if the slot already exists
// (as is the case in hot-reloading).
static void P_impl_unload(struct Plugin *plugin)
{
    plugin->on_deinit();

    // Unlink the plugin from the list.
    if (plugin->prev) {
        plugin->prev->next = plugin->next;
    }
    if (plugin->next) {
        plugin->next->prev = plugin->prev;
    }

    FreeLibrary(plugin->handle);
}


struct Plugin *P_find(const char *path)
{
    struct Plugin *plugin = plugins;
    while (plugin) {
        if (strcmp(plugin->path, path) == 0) {
            return plugin;
        }
        plugin = plugin->next;
    }
    return NULL;
}


int P_reload(const char *path, struct Plugin **plugin)
{
    *plugin = P_find(path);
    if (*plugin) {
        P_impl_unload(*plugin);
    }

    HANDLE handle = LoadLibraryA(path);
    if (!handle) {
        fprintf(stderr, "ERROR: Failed to load module.\n");
        return EXIT_FAILURE;
    }

    if (!*plugin) {
        *plugin = malloc(sizeof(struct Plugin));
        assert(*plugin);
        (*plugin)->path = NULL;
    }

    // Link-up the new plugin at the beginning of the list.
    (*plugin)->next = plugins;
    (*plugin)->prev = NULL;
    if (plugins) {
        plugins->prev = *plugin;
    }

    (*plugin)->handle = handle;
    (*plugin)->on_init = (int(*)(void))GetProcAddress(handle, "on_init");
    assert((*plugin)->on_init);
    (*plugin)->on_deinit = (void(*)(void))GetProcAddress(handle, "on_deinit");
    assert((*plugin)->on_deinit);
    if (!(*plugin)->path) {
        (*plugin)->path = strdup(path);
    }
    assert((*plugin)->path);

    int result = (*plugin)->on_init();
    if (result) {
        fprintf(stderr, "ERROR: Failed to initialize.\n");
        P_unload(*plugin);
        return result;
    }
    return EXIT_SUCCESS;
}


void P_unload(struct Plugin *plugin)
{
    P_impl_unload(plugin);
    free((void *)plugin->path);  // Cast away the const
}
