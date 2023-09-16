#include <windows.h>
#include "database.h"
#include "plugin.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
    if (D_connect()) {
        return EXIT_FAILURE;
    }

    struct R_State *state;
    const struct R_RendererDesc desc = {
        .type = R_TYPE_VULKAN,
        .nCmdShow = nCmdShow,
        .hInstance = hInstance,
        .width = 800,
        .height = 600
    };
    if (R_create(&state, &desc)) {
        return EXIT_FAILURE;
    }

    struct Plugin *plugin;
    if (P_reload("flip", &plugin)) {
        return EXIT_FAILURE;
    }

    for (;;) {
        if (!R_update(state)) {
            break;
        }
    }
    D_disconnect();
    return EXIT_SUCCESS;
}