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

    struct D_Table *particlesTable;
    D_find_or_create_table("Particles", &particlesTable);
    particlesTable->rowStride = 1024;
    char *buffer = malloc(1024*1024*1024);
    D_insert_rows(particlesTable, 1024*1024, buffer);


    //struct Particle particle;
    //D_append_row(particlesTable, &particle);

    fprintf(stderr, "FLIP loaded!\n");
    return EXIT_SUCCESS;
}


void on_deinit(void)
{
    D_disconnect();
}
