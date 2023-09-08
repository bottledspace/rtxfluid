struct Plugin {
    const char *path;
    int (*on_init)(void);
    void (*on_deinit)(void);

    // == Internal ==
    void *handle;
    struct Plugin *next;
    struct Plugin *prev;
};


// Search for a previously loaded plugin with the given path.
struct Plugin *P_find(const char *path);

// Load a plugin with the given path. The path should not include a file
// extension (such as .DLL or .so) in order to remain platform independent.
// If the plugin was previously loaded, it is unloaded and then loaded again.
// If successful plugin is filled with a pointer to the plugin structure.
// Returns nonzero on failure.
int P_reload(const char *path, struct Plugin **plugin);

// Unloads a previously loaded plugin.
void P_unload(struct Plugin *plugin);