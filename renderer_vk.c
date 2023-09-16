#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "renderer.h"
#include <stdio.h>
#include <assert.h>
#include <malloc.h>

#define R_SWAPCHAIN_COUNT 2
#define R_WNDCLASS "86875c01-feaa-45f3-a273-eaf6442fe84d"
#define R_STATE(hWnd) ((struct R_StateVK*)GetWindowLongPtr(hWnd, GWLP_USERDATA))

struct R_StateVK {
	int type;
	int (*on_update)(struct R_State *);
	int width;
	int height;
	HINSTANCE hInstance;
	int nCmdShow;
	HWND hWnd;
	HDC hDC;
	VkInstance vkInstance;
	VkPhysicalDevice vkPhysicalDevice;
	VkDevice vkDevice;
	VkQueue vkGraphicsQueue;
	VkQueue vkPresentQueue;
	VkSurfaceKHR vkSurface;
	VkSwapchainKHR vkSwapchain;
	VkImage vkSwapchainImages[R_SWAPCHAIN_COUNT];
};


static void R_impl_draw(struct R_State *state)
{
	//struct R_StateVK *stateVK = (struct R_StateVK *)state;

}


int R_impl_update_VK(struct R_State* state)
{
	//struct R_StateVK *stateVK = (struct R_StateVK *)state;
	MSG msg;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		if (msg.message == WM_QUIT) {
			return 0;
		}
		DispatchMessage(&msg);
	}

	R_impl_draw(state);
	return 1;
}


static VKAPI_ATTR VkBool32 VKAPI_CALL R_impl_print_debug_message(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData)
{
    OutputDebugStringA(pCallbackData->pMessage);
    return VK_FALSE;
}


static void R_impl_create_instance(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	VkApplicationInfo appInfo;
	memset(&appInfo, 0, sizeof(VkApplicationInfo));
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = desc->title;
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	const char *extensionNames[] = {
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};
	const char *validationLayerNames[] = {
		"VK_LAYER_KHRONOS_validation"
	};

	VkInstanceCreateInfo instanceCreateInfo;
	memset(&instanceCreateInfo, 0, sizeof(VkInstanceCreateInfo));
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	instanceCreateInfo.enabledExtensionCount = ARRAYSIZE(extensionNames);
	instanceCreateInfo.ppEnabledExtensionNames = extensionNames;
	instanceCreateInfo.enabledLayerCount = ARRAYSIZE(validationLayerNames);
	instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
	memset(&debugCreateInfo, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT));
	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
									| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
									| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
				                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = R_impl_print_debug_message;
	debugCreateInfo.pNext = &debugCreateInfo;

	VkResult result = vkCreateInstance(&instanceCreateInfo, NULL, &state->vkInstance);
	assert(result == VK_SUCCESS);
}


static void R_impl_create_device(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	VkResult result;

	uint32_t deviceCount = 0;
	(void)vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, NULL);
	assert(deviceCount);
	VkPhysicalDevice *physicalDevices = _malloca(sizeof(VkPhysicalDevice) * deviceCount);
	assert(physicalDevices);
	result = vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, physicalDevices);
	assert(result == VK_SUCCESS);
	state->vkPhysicalDevice = physicalDevices[0];

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(state->vkPhysicalDevice, &queueFamilyCount, NULL);
	assert(queueFamilyCount);
	VkQueueFamilyProperties *queueFamilies = _malloca(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
	assert(queueFamilies);
	vkGetPhysicalDeviceQueueFamilyProperties(state->vkPhysicalDevice, &queueFamilyCount, queueFamilies);

	uint32_t familyIndex;
	for (familyIndex = 0; familyIndex < queueFamilyCount; familyIndex++) {
		if (queueFamilies[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			break;
		}
	}
	assert(familyIndex < queueFamilyCount);

	VkDeviceQueueCreateInfo queueCreateInfo;
	memset(&queueCreateInfo, 0, sizeof(VkDeviceQueueCreateInfo));
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = familyIndex;
	queueCreateInfo.queueCount = 1;

	VkPhysicalDeviceFeatures deviceFeatures;
	memset(&deviceFeatures, 0, sizeof(VkPhysicalDeviceFeatures));

	VkDeviceCreateInfo deviceCreateInfo;
	memset(&deviceCreateInfo, 0, sizeof(VkDeviceCreateInfo));
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
	result = vkCreateDevice(state->vkPhysicalDevice, &deviceCreateInfo, NULL, &state->vkDevice);
	assert(result == VK_SUCCESS);
	vkGetDeviceQueue(state->vkDevice, familyIndex, 0, &state->vkGraphicsQueue);

	_freea(physicalDevices);
	_freea(queueFamilies);
}


static void R_impl_create_swapchain(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	VkResult result;

	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo;
	memset(&surfaceCreateInfo, 0, sizeof(VkWin32SurfaceCreateInfoKHR));
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hwnd = state->hWnd;
	surfaceCreateInfo.hinstance = state->hInstance;
	vkCreateWin32SurfaceKHR(state->vkInstance, &surfaceCreateInfo, NULL, &state->vkSurface);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkDevice, state->vkSurface, &formatCount, NULL);
	assert(formatCount);
	VkSurfaceFormatKHR *surfaceFormats = _malloca(sizeof(VkSurfaceFormatKHR) * formatCount);
	assert(surfaceFormats);
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkDevice, state->vkSurface, &formatCount, NULL);

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(state->vkDevice, state->vkSurface, &presentModeCount, NULL);
	assert(presentModeCount);
	VkPresentModeKHR *presentModes = _malloca(sizeof(VkPresentModeKHR) * presentModeCount);
	assert(presentModes);
	vkGetPhysicalDeviceSurfacePresentModesKHR(state->vkDevice, state->vkSurface, &presentModeCount, presentModes);

	uint32_t formatIndex;
	for (formatIndex = 0; formatIndex < formatCount; formatIndex++) {
		if (surfaceFormats[formatIndex].format == VK_FORMAT_B8G8R8A8_SRGB
		 && surfaceFormats[formatIndex].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			break;
		}
	}
	assert(formatIndex < formatCount);

	uint32_t presentModeIndex;
	for (presentModeIndex = 0; presentModeIndex < presentModeCount; presentModeIndex++) {
		if (presentModes[presentModeIndex] == VK_PRESENT_MODE_MAILBOX_KHR) {
			break;
		}
	}

	VkExtent2D extent;
	extent.width = state->width;
	extent.height = state->height;

	VkSwapchainCreateInfoKHR swapchainCreateInfo;
	memset(&swapchainCreateInfo, 0, sizeof(VkSwapchainCreateInfoKHR));
	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.surface = state->vkSurface;
	swapchainCreateInfo.minImageCount = R_SWAPCHAIN_COUNT;
	swapchainCreateInfo.imageFormat = surfaceFormats[formatIndex].format;
	swapchainCreateInfo.imageColorSpace = surfaceFormats[formatIndex].colorSpace;
	memcpy(&swapchainCreateInfo.imageExtent, &extent, sizeof(VkExtent2D));
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = NULL;

	result = vkCreateSwapchainKHR(state->vkDevice, &swapchainCreateInfo, NULL, &state->vkSwapchain);
	assert(result == VK_SUCCESS);

	uint32_t imageCount = R_SWAPCHAIN_COUNT;
	result = vkGetSwapchainImagesKHR(state->vkDevice, state->vkSwapchain, &imageCount, state->vkSwapchainImages);
	assert(result == VK_SUCCESS);


	_freea(presentModes);
	_freea(surfaceFormats);
}


static int R_impl_on_create(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	R_impl_create_instance(state, desc);
	R_impl_create_device(state, desc);
	R_impl_create_swapchain(state, desc);

	return EXIT_SUCCESS;
}


static LRESULT CALLBACK R_impl_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		const CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
		const struct R_RendererDesc *desc = (const struct R_RendererDesc *)cs->lpCreateParams;

		struct R_StateVK *state = calloc(1, sizeof(struct R_StateVK));
		assert(state);
		state->hWnd = hWnd;
		state->type = R_TYPE_VULKAN;
		state->width = cs->cx;
		state->height = cs->cy;
		state->hInstance = desc->hInstance;
		state->hDC = GetDC(state->hWnd);
		state->on_update = R_impl_update_VK;
		R_impl_on_create(state, desc);

		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)state);
		break;
	}
	case WM_SIZE: {
		struct R_StateVK *state = R_STATE(hWnd);
		state->width = LOWORD(lParam);
		state->height = HIWORD(lParam);
		break;
	}
	case WM_DESTROY: {
		struct R_StateVK *state = R_STATE(hWnd);
		vkDestroySwapchainKHR(state->vkDevice, state->vkSwapchain, NULL);
		vkDestroySurfaceKHR(state->vkInstance, state->vkSurface, NULL);
		vkDestroyDevice(state->vkDevice, NULL);
		vkDestroyInstance(state->vkInstance, NULL);
		free(state);
		break;
	}
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}


int R_create_VK(struct R_State **state, const struct R_RendererDesc *desc)
{	
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = R_impl_WndProc;
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
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, desc->hInstance, (LPVOID)desc);
	assert(hWnd);

	(void)ShowWindow(hWnd, desc->nCmdShow);
	(void)UpdateWindow(hWnd);

	*state = (struct R_State *)R_STATE(hWnd);
	return EXIT_SUCCESS;
}
