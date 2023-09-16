#define R_TYPE_NONE   0
#define R_TYPE_VULKAN 2


struct R_RendererDesc {
	int type;
	const char *title;
	unsigned width;
	unsigned height;
#ifdef _WIN32
	int nCmdShow;          // = SW_*
	HINSTANCE hInstance;
#endif // _WIN32
};

struct R_State {
	int type;
	int (*on_update)(struct R_State *);
	// Private members follow.
};


int R_create_VK(struct R_State **state, const struct R_RendererDesc *desc);


static inline int R_create(struct R_State **state, const struct R_RendererDesc *desc)
{
	switch (desc->type) {
	case R_TYPE_VULKAN:
		return R_create_VK(state, desc);
	default:
		return EXIT_FAILURE;
	}
}


static inline int R_update(struct R_State* state)
{
	return state->on_update(state);
}
