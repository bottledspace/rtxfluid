#include "scene.h"
#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char **argv)
{
    if (S_init()) {
        return EXIT_FAILURE;
    }

    struct Plugin *plugin;
    if (P_reload("flip", &plugin)) {
        return EXIT_FAILURE;
    }

    S_deinit();
    return EXIT_SUCCESS;
}