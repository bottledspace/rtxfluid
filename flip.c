#include "database.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <windows.h>

struct Particle {
    float x, y;
    float u, v;
};

int on_init(void)
{
    if (D_connect()) {
        return EXIT_FAILURE;
    }

    struct D_TableDescriptor particlesTable;
    strcpy(particlesTable.name, "Particles");
    (void)D_create_table(&particlesTable);

    fprintf(stderr, "FLIP loaded!\n");
    return EXIT_SUCCESS;
}


void on_deinit(void)
{
    D_disconnect();
}
