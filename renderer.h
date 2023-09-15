#define R_TYPE_NONE  0
#define R_TYPE_GL1   1 // OpenGL 1.1


struct R_RendererDesc {
	int type;
	const char *title;
#ifdef _WIN32
	int nCmdShow;          // = SW_*
	HINSTANCE hInstance;
#endif // _WIN32
};

struct R_State {
	int type;
	// Private members follow.
};

int R_create_GL1(struct R_State **state, const struct R_RendererDesc *desc);
int R_update_GL1(struct R_State *state);

static int R_create(struct R_State **state, const struct R_RendererDesc *desc)
{
	switch (desc->type) {
	case R_TYPE_GL1:
		return R_create_GL1(state, desc);
	default:
		return EXIT_FAILURE;
	}
}

static int R_update(struct R_State* state)
{
	switch (state->type) {
	case R_TYPE_GL1:
		return R_update_GL1(state);
	default:
		return 0;
	}
}
