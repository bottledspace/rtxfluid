#include "database.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <windows.h>

struct Particle {
    float x, y;
    float u, v;
};

D_TABLE(Particle) {
D_COLUMN(x, float),
D_COLUMN(y, float),
D_COLUMN(u, float),
D_COLUMN(v, float)
};


struct Grid {
    size_t rows, cols;
    size_t data;
};

D_TABLE(Grid) {
D_COLUMN(rows, size_t),
D_COLUMN(cols, size_t),
D_COLUMN(data, size_t)
};

 __declspec(dllexport) int on_init(void)
{
    if (D_connect()) {
        return EXIT_FAILURE;
    }

    struct D_Table *particleTable;
    D_create_table(&particleTable, D_TABLE_DESC(Particle));
    struct D_Table *gridTable;
    D_create_table(&gridTable, D_TABLE_DESC(Grid));

    struct Particle *particle;
    D_insert_rows(particleTable, 10000, (void **)&particle);
    for (int x = 0; x < 100; x++)
    for (int y = 0; y < 100; y++) {
        particle->x = x;
        particle->y = y;
        particle++;
    }

    struct Grid *grid;
    D_insert_rows(gridTable, 1, (void **)&grid);
    grid->cols = 100;
    grid->rows = 100;
    grid->data = D_blob_alloc(100*100);

    fprintf(stderr, "FLIP loaded!\n");
    return EXIT_SUCCESS;
}


 __declspec(dllexport) void on_deinit(void)
{
    D_disconnect();
}
