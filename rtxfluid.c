#include "database.h"
#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char **argv)
{
    if (D_connect()) {
        return EXIT_FAILURE;
    }

    struct Plugin *plugin;
    if (P_reload("flip", &plugin)) {
        return EXIT_FAILURE;
    }

    D_disconnect();
    return EXIT_SUCCESS;
}