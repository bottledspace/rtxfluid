#include <Windows.h>
#include "database.h"
#include "plugin.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPCSTR lpszCmdLine, int nCmdShow)
{
    if (D_connect()) {
        return EXIT_FAILURE;
    }

    struct R_State *state;
    {
        struct R_RendererDesc desc = {
            .width = 1600,
            .height = 1200,
            .nCmdShow = nCmdShow,
            .title = "RTX Fluid",
            .type = R_TYPE_VULKAN
        };
        R_create(&state, &desc);
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