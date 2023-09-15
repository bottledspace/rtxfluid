#include <windows.h>
#include "renderer.h"
#include <GL/gl.h>
#include <stdio.h>
#include <assert.h>

#define R_WNDCLASS "86875c01-feaa-45f3-a273-eaf6442fe84d"
#define R_STATE(hWnd) ((struct R_StateGL*)GetWindowLongPtr(hWnd, GWLP_USERDATA))

struct R_StateGL {
	int type;
	int width;
	int height;
	HINSTANCE hInstance;
	int nCmdShow;
	HWND hWnd;
	HDC hDC;
	HGLRC hRC;
};


static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		const CREATESTRUCT *cs = ((CREATESTRUCT*)lParam);
		struct R_StateGL *state = calloc(1, sizeof(struct R_StateGL));
		state->type = R_TYPE_GL1;
		state->width = cs->cx;
		state->height = cs->cy;
		state->hInstance = cs->hInstance;
		state->hWnd = hWnd;
		state->hDC = GetDC(hWnd);

		PIXELFORMATDESCRIPTOR pfd;
		memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
		pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 24;
		pfd.cDepthBits = 32;
		pfd.iLayerType = PFD_MAIN_PLANE;

		int pf = ChoosePixelFormat(state->hDC, &pfd);
		assert(pf);
		(void)SetPixelFormat(state->hDC, pf, &pfd);

		state->hRC = wglCreateContext(state->hDC);
		assert(state->hRC);
		(void)wglMakeCurrent(state->hDC, state->hRC);

		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)state);
		break;
	}
	case WM_SIZE: {
		struct R_StateGL *state = R_STATE(hWnd);
		state->width = LOWORD(lParam);
		state->height = HIWORD(lParam);
		break;
	}
	case WM_DESTROY: {
		struct R_StateGL *state = R_STATE(hWnd);
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(state->hRC);
		free(state);
		break;
	}
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


int R_create_GL1(struct R_State **state, const struct R_RendererDesc *desc)
{	
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = desc->hInstance;
	wc.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_APPLICATION));
	wc.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW));
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = R_WNDCLASS;
	wc.hIconSm = wc.hIcon;

	ATOM wcAtom = RegisterClassEx(&wc);
	assert(wcAtom);

	HWND hWnd = CreateWindowA(MAKEINTATOM(wcAtom), desc->title, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, desc->hInstance, NULL);
	assert(hWnd);

	(void)ShowWindow(hWnd, desc->nCmdShow);
	(void)UpdateWindow(hWnd);

	*state = (struct R_State *)R_STATE(hWnd);
	return EXIT_SUCCESS;
}


static void R_impl_draw(struct R_State *state)
{
	struct R_StateGL *stateGL = (struct R_StateGL *)state;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, stateGL->width, stateGL->height, 0, 0, 1);
	glMatrixMode(GL_MODELVIEW);

	glClear(GL_COLOR_BUFFER_BIT);
}


int R_update_GL1(struct R_State* state)
{
	struct R_StateGL *stateGL = (struct R_StateGL *)state;
	MSG msg;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		if (msg.message == WM_QUIT) {
			return 0;
		}
		DispatchMessage(&msg);
	}

	R_impl_draw(state);
	SwapBuffers(stateGL->hDC);
	return 1;
}

