#include "scene.h"
#include <stdlib.h>
#include <stdio.h>


#define ACCEL_ID     "flip:accel"
#define PARTICLES_ID "flip:particles"

int on_init(void)
{
    if (S_init()) {
        return EXIT_FAILURE;
    }
    struct Node *accel = S_find_or_create(ACCEL_ID);
    struct Node *particles = S_find_or_create(PARTICLES_ID);
    (void)accel;
    (void)particles;
    fprintf(stderr, "FLIP loaded!\n");
    return EXIT_SUCCESS;
}

void on_deinit(void)
{
    S_deinit();
}
