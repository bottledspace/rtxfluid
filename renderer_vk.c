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
	VkImageView vkSwapchainViews[R_SWAPCHAIN_COUNT];
	VkFramebuffer vkSwapchainFramebuffers[R_SWAPCHAIN_COUNT];
	VkSemaphore vkImageAvailableSemaphore;
	VkSemaphore vkRenderFinishedSemaphore;
	VkFence vkFence;
	VkCommandPool vkCommandPool;
	VkCommandBuffer vkCommandBuffer;
	VkRenderPass vkRenderPass;
	VkPipelineLayout vkPipelineLayout;
	VkPipeline vkPipeline;
	VkImage vkRaytraceImage;
	VkDescriptorSet vkDescriptorSet;
	VkDescriptorPool vkDescriptorPool;
	VkDescriptorSetLayout vkDescriptorSetLayout;
	VkBuffer vkRaygenBuffer;
	VkBuffer vkMissBuffer;
	VkBuffer vkClosestHitBuffer;
	VkAccelerationStructureKHR blas;
	VkAccelerationStructureKHR tlas;

	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR_;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR_;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR_;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_;
};


static void R_impl_draw(struct R_State *state_)
{
	struct R_StateVK *state = (struct R_StateVK *)state_;

	vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	vkResetFences(state->vkDevice, 1, &state->vkFence);

	uint32_t imageIndex;
    vkAcquireNextImageKHR(state->vkDevice, state->vkSwapchain, UINT64_MAX,
		state->vkImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	vkResetCommandBuffer(state->vkCommandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo = {0};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = NULL;
	vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
	
	VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry = {0};
	VkStridedDeviceAddressRegionKHR missShaderSbtEntry = {0};
	VkStridedDeviceAddressRegionKHR hitShaderSbtEntry = {0};
	VkStridedDeviceAddressRegionKHR callableShaderSbtEntry = {0};

	VkBufferDeviceAddressInfoKHR bufferDeviceAI = {0};
	bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAI.buffer = state->vkRaygenBuffer;
	raygenShaderSbtEntry.size = 32;
	raygenShaderSbtEntry.stride = 32;
	raygenShaderSbtEntry.deviceAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAI);
	
	bufferDeviceAI.buffer = state->vkMissBuffer;
	missShaderSbtEntry.size = 32;
	missShaderSbtEntry.stride = 32;
	missShaderSbtEntry.deviceAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAI);

	bufferDeviceAI.buffer = state->vkClosestHitBuffer;
	hitShaderSbtEntry.size = 32;
	hitShaderSbtEntry.stride = 32;
	hitShaderSbtEntry.deviceAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAI);
	
	vkCmdBindPipeline(state->vkCommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, state->vkPipeline);
	vkCmdBindDescriptorSets(state->vkCommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, state->vkPipelineLayout, 0, 1, &state->vkDescriptorSet, 0, NULL);
	state->vkCmdTraceRaysKHR_(state->vkCommandBuffer, &raygenShaderSbtEntry, &missShaderSbtEntry,
		&hitShaderSbtEntry, &callableShaderSbtEntry, state->width, state->height, 1);
		
	VkImageMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = state->vkSwapchainImages[imageIndex];
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);

	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.image = state->vkRaytraceImage;
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);
	
	VkImageCopy regions = {0};
	regions.extent.width = state->width;
	regions.extent.height = state->height;
	regions.extent.depth = 1;
	regions.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regions.dstSubresource.layerCount = 1;
	regions.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regions.srcSubresource.layerCount = 1;
	vkCmdCopyImage(state->vkCommandBuffer, state->vkRaytraceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		state->vkSwapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &regions);

	/*VkClearColorValue clearColor = {0};
	VkImageSubresourceRange subrange = {0};
	subrange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subrange.layerCount = 1;
	vkCmdClearColorImage(state->vkCommandBuffer, state->vkSwapchainImages[imageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subrange);*/

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.image = state->vkSwapchainImages[imageIndex];
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);
	
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.image = state->vkRaytraceImage;
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);
	
	vkEndCommandBuffer(state->vkCommandBuffer);

	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {0};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &state->vkImageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &state->vkCommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &state->vkRenderFinishedSemaphore;
	vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

	VkPresentInfoKHR presentInfo = {0};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &state->vkRenderFinishedSemaphore;

	VkSwapchainKHR swapChains[] = { state->vkSwapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	vkQueuePresentKHR(state->vkGraphicsQueue, &presentInfo);
}


int R_impl_update_VK(struct R_State* state_)
{
	MSG msg;

	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		if (msg.message == WM_QUIT) {
			return 0;
		}
		DispatchMessage(&msg);
	}
	R_impl_draw(state_);
	Sleep(10);
	return 1;
}


static VKAPI_ATTR VkBool32 VKAPI_CALL R_impl_print_debug_message(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData)
{
    OutputDebugStringA(pCallbackData->pMessage);
	OutputDebugStringA("\n");
    return VK_FALSE;
}


static void R_impl_create_instance(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	VkApplicationInfo appInfo = {0};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = desc->title;
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	const char *extensionNames[] = {
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		VK_KHR_SURFACE_EXTENSION_NAME
	};
	const char *validationLayerNames[] = {
		"VK_LAYER_KHRONOS_validation"
	};

	VkInstanceCreateInfo instanceCreateInfo = {0};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	instanceCreateInfo.enabledExtensionCount = ARRAYSIZE(extensionNames);
	instanceCreateInfo.ppEnabledExtensionNames = extensionNames;
	instanceCreateInfo.enabledLayerCount = ARRAYSIZE(validationLayerNames);
	instanceCreateInfo.ppEnabledLayerNames = validationLayerNames;

	VkResult result = vkCreateInstance(&instanceCreateInfo, NULL, &state->vkInstance);
	assert(result == VK_SUCCESS);
	state->vkCreateRayTracingPipelinesKHR_ = (PFN_vkCreateRayTracingPipelinesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateRayTracingPipelinesKHR");
	state->vkCmdTraceRaysKHR_ = (PFN_vkCmdTraceRaysKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCmdTraceRaysKHR");
	state->vkGetRayTracingShaderGroupHandlesKHR_ = (PFN_vkGetRayTracingShaderGroupHandlesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetRayTracingShaderGroupHandlesKHR");
	state->vkGetBufferDeviceAddressKHR_ = (PFN_vkGetBufferDeviceAddressKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetBufferDeviceAddressKHR");
	state->vkCreateAccelerationStructureKHR_ = (PFN_vkCreateAccelerationStructureKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateAccelerationStructureKHR");
	state->vkCmdBuildAccelerationStructuresKHR_ = (PFN_vkCmdBuildAccelerationStructuresKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCmdBuildAccelerationStructuresKHR");
	state->vkGetAccelerationStructureBuildSizesKHR_ = (PFN_vkGetAccelerationStructureBuildSizesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetAccelerationStructureBuildSizesKHR");
	state->vkGetAccelerationStructureDeviceAddressKHR_ = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetAccelerationStructureDeviceAddressKHR");


    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(state->vkInstance, "vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(state->vkInstance, "vkDestroyDebugUtilsMessengerEXT");
	VkDebugUtilsMessengerCreateInfoEXT callback1 = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
        .messageType= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		              VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
        .pfnUserCallback = R_impl_print_debug_message,
        .pUserData = NULL
    };
	VkDebugUtilsMessengerEXT cb1;
    pfnCreateDebugUtilsMessengerEXT(state->vkInstance, &callback1, NULL, &cb1);
    
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

	VkDeviceQueueCreateInfo queueCreateInfo = {0};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = familyIndex;
	queueCreateInfo.queueCount = 1;
	float queuePriority = 1.0f;
	queueCreateInfo.pQueuePriorities = &queuePriority;
	
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = {0};
	accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	accelFeatures.accelerationStructure = VK_TRUE;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingFeatures = {0};
	raytracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	raytracingFeatures.rayTracingPipeline = VK_TRUE;
	raytracingFeatures.pNext = &accelFeatures;
	
	VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufferFeatures = {0};
	bufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
	bufferFeatures.bufferDeviceAddress = VK_TRUE;
	bufferFeatures.pNext = &raytracingFeatures;

	VkPhysicalDeviceFeatures2 deviceFeatures2 = {0};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	deviceFeatures2.pNext = &bufferFeatures;

	const char *extensionNames[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME 
	};

	VkDeviceCreateInfo deviceCreateInfo = {0};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = &deviceFeatures2;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pEnabledFeatures = NULL;
	deviceCreateInfo.enabledExtensionCount = ARRAYSIZE(extensionNames);
	deviceCreateInfo.ppEnabledExtensionNames = extensionNames;
	result = vkCreateDevice(state->vkPhysicalDevice, &deviceCreateInfo, NULL, &state->vkDevice);
	assert(result == VK_SUCCESS);
	vkGetDeviceQueue(state->vkDevice, familyIndex, 0, &state->vkGraphicsQueue);

	VkCommandPoolCreateInfo poolInfo = {0};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = familyIndex;
	vkCreateCommandPool(state->vkDevice, &poolInfo, NULL, &state->vkCommandPool);

	VkCommandBufferAllocateInfo allocInfo = {0};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = state->vkCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	vkAllocateCommandBuffers(state->vkDevice, &allocInfo, &state->vkCommandBuffer);

	_freea(physicalDevices);
	_freea(queueFamilies);
}


static void R_impl_create_swapchain(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	VkResult result;

	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {0};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hwnd = state->hWnd;
	surfaceCreateInfo.hinstance = state->hInstance;
	vkCreateWin32SurfaceKHR(state->vkInstance, &surfaceCreateInfo, NULL, &state->vkSurface);

	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkPhysicalDevice, state->vkSurface, &formatCount, NULL);
	assert(formatCount);
	VkSurfaceFormatKHR *surfaceFormats = _malloca(sizeof(VkSurfaceFormatKHR) * formatCount);
	assert(surfaceFormats);
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkPhysicalDevice, state->vkSurface, &formatCount, surfaceFormats);

	uint32_t presentModeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(state->vkPhysicalDevice, state->vkSurface, &presentModeCount, NULL);
	assert(presentModeCount);
	VkPresentModeKHR *presentModes = _malloca(sizeof(VkPresentModeKHR) * presentModeCount);
	assert(presentModes);
	vkGetPhysicalDeviceSurfacePresentModesKHR(state->vkPhysicalDevice, state->vkSurface, &presentModeCount, presentModes);

	uint32_t formatIndex;
	for (formatIndex = 0; formatIndex < formatCount; formatIndex++) {
		if (surfaceFormats[formatIndex].format == VK_FORMAT_B8G8R8A8_UNORM
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

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state->vkPhysicalDevice, state->vkSurface, &surfaceCapabilities);
	assert(result == VK_SUCCESS);

	state->width = surfaceCapabilities.currentExtent.width;
	state->height = surfaceCapabilities.currentExtent.height;

	VkSwapchainCreateInfoKHR swapchainCreateInfo = {0};
	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.surface = state->vkSurface;
	swapchainCreateInfo.minImageCount = R_SWAPCHAIN_COUNT;
	swapchainCreateInfo.imageFormat = surfaceFormats[formatIndex].format;
	swapchainCreateInfo.imageColorSpace = surfaceFormats[formatIndex].colorSpace;
	swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.queueFamilyIndexCount = 0;
    swapchainCreateInfo.pQueueFamilyIndices = NULL;
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = surfaceCapabilities.supportedCompositeAlpha; //VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = presentModes[presentModeIndex];
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
	result = vkCreateSwapchainKHR(state->vkDevice, &swapchainCreateInfo, NULL, &state->vkSwapchain);
	assert(result == VK_SUCCESS);

	uint32_t imageCount = R_SWAPCHAIN_COUNT;
	result = vkGetSwapchainImagesKHR(state->vkDevice, state->vkSwapchain, &imageCount, state->vkSwapchainImages);
	assert(result == VK_SUCCESS);

	VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = surfaceFormats[formatIndex].format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {0};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	
	VkSubpassDescription subpass = {0};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {0};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	vkCreateRenderPass(state->vkDevice, &renderPassInfo, NULL, &state->vkRenderPass);

	for (size_t i = 0; i < R_SWAPCHAIN_COUNT; i++) {
		VkImageViewCreateInfo imageViewCreateInfo = {0};
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.image = state->vkSwapchainImages[i];
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = surfaceFormats[formatIndex].format;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		vkCreateImageView(state->vkDevice, &imageViewCreateInfo, NULL, &state->vkSwapchainViews[i]);

		VkFramebufferCreateInfo framebufferInfo = {0};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = state->vkRenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &state->vkSwapchainViews[i];
		framebufferInfo.width = state->width;
		framebufferInfo.height = state->height;
		framebufferInfo.layers = 1;
		vkCreateFramebuffer(state->vkDevice, &framebufferInfo, NULL, &state->vkSwapchainFramebuffers[i]);
	}

	VkSemaphoreCreateInfo semaphoreCreateInfo = {0};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	vkCreateSemaphore(state->vkDevice, &semaphoreCreateInfo, NULL, &state->vkRenderFinishedSemaphore);
	vkCreateSemaphore(state->vkDevice, &semaphoreCreateInfo, NULL, &state->vkImageAvailableSemaphore);

	// Create the fence in a signalled state so we don't wait indefinitely
	// on the first frame.
	VkFenceCreateInfo fenceCreateInfo = {0};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(state->vkDevice, &fenceCreateInfo, NULL, &state->vkFence);

	_freea(presentModes);
	_freea(surfaceFormats);
}


static void R_impl_create_blas(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	VkAabbPositionsKHR aabb;
	aabb.minX = aabb.minY = aabb.minZ = -0.5;
	aabb.maxX = aabb.maxY = aabb.maxZ = 0.5;
	aabb.minZ += 2; aabb.maxZ += 2;

	VkBuffer aabbBuffer;
	{
		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		bufferCreateInfo.size = 32;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &aabbBuffer);
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, aabbBuffer, &memoryRequirements);
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if (((1 << i) & memoryRequirements.memoryTypeBits)
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		void *memoryPtr;
		vkMapMemory(state->vkDevice, memory, 0, sizeof(VkAabbPositionsKHR), 0, &memoryPtr);
		memcpy(memoryPtr, &aabb, sizeof(aabb));
		vkUnmapMemory(state->vkDevice, memory);
		vkBindBufferMemory(state->vkDevice, aabbBuffer, memory, 0);
	}

	VkBufferDeviceAddressInfoKHR bufferAddressInfo = {0};
	bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
	bufferAddressInfo.buffer = aabbBuffer;

	VkAccelerationStructureGeometryKHR accelerationStructureGeometry = {0};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.flags = 0;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
	accelerationStructureGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
	accelerationStructureGeometry.geometry.aabbs.data.deviceAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferAddressInfo);
	accelerationStructureGeometry.geometry.aabbs.stride = sizeof(aabb);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {0};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	
	const uint32_t numPrims = 1;
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = {0};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	state->vkGetAccelerationStructureBuildSizesKHR_(state->vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo, &numPrims, &accelerationStructureBuildSizesInfo);

	VkBuffer accelBuffer;
	{
		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &accelBuffer);
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, accelBuffer, &memoryRequirements);
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((1 << i) & memoryRequirements.memoryTypeBits
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		vkBindBufferMemory(state->vkDevice, accelBuffer, memory, 0);
	}

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = accelBuffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	state->vkCreateAccelerationStructureKHR_(state->vkDevice, &accelerationStructureCreateInfo, NULL, &state->blas);
	
	VkBuffer scratchBuffer;
	{
		VkBufferCreateInfo bufferCreateInfo3 = {0};
		bufferCreateInfo3.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo3.size = accelerationStructureBuildSizesInfo.buildScratchSize;
		bufferCreateInfo3.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo3, NULL, &scratchBuffer);
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, scratchBuffer, &memoryRequirements);
		
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((1 << i) & memoryRequirements.memoryTypeBits
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		vkBindBufferMemory(state->vkDevice, scratchBuffer, memory, 0);
	}
	VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo = {0};
	bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	bufferDeviceAddressInfo.buffer = scratchBuffer;
	VkDeviceAddress scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAddressInfo);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {0};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.dstAccelerationStructure = state->blas;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBufferAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {0};
	accelerationStructureBuildRangeInfo.primitiveCount = 1;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;

	VkAccelerationStructureBuildRangeInfoKHR *accelerationBuildStructureRangeInfos[] = {
		&accelerationStructureBuildRangeInfo
	};

	{
		VkCommandBufferBeginInfo beginInfo = {0};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
		
		state->vkCmdBuildAccelerationStructuresKHR_(state->vkCommandBuffer, 1,
			&accelerationBuildGeometryInfo, accelerationBuildStructureRangeInfos);
		
		vkEndCommandBuffer(state->vkCommandBuffer);

		vkResetFences(state->vkDevice, 1, &state->vkFence);

		VkSubmitInfo submitInfo = {0};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &state->vkCommandBuffer;
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

		vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	}
}


static void R_impl_create_tlas(struct R_StateVK* state, const struct R_RendererDesc* desc)
{
	VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo = {0};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = state->blas;

	VkAccelerationStructureInstanceKHR instance = {0};
	instance.transform = transformMatrix;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = 0;
	instance.accelerationStructureReference = state->vkGetAccelerationStructureDeviceAddressKHR_(state->vkDevice, &accelerationDeviceAddressInfo);

	VkBuffer instancesBuffer;
	VkDeviceAddress instancesBufferAddress;
	{
		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		bufferCreateInfo.size = sizeof(VkAccelerationStructureInstanceKHR);
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &instancesBuffer);
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, instancesBuffer, &memoryRequirements);
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((1 << i) & memoryRequirements.memoryTypeBits
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		void *memoryPtr;
		vkMapMemory(state->vkDevice, memory, 0, sizeof(instance), 0, &memoryPtr);
		memcpy(memoryPtr, &instance, sizeof(instance));
		vkUnmapMemory(state->vkDevice, memory);
		vkBindBufferMemory(state->vkDevice, instancesBuffer, memory, 0);
	
		VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo = {0};
		bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		bufferDeviceAddressInfo.buffer = instancesBuffer;
		instancesBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAddressInfo);
	}
	
	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress = {0};
	instanceDataDeviceAddress.deviceAddress = instancesBufferAddress;

	VkAccelerationStructureGeometryKHR accelerationStructureGeometry = {0};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {0};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	uint32_t primitive_count = 1;
	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = {0};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	state->vkGetAccelerationStructureBuildSizesKHR_(state->vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo, &primitive_count, &accelerationStructureBuildSizesInfo);

	VkBuffer accelBuffer;
	{
		VkBufferCreateInfo bufferCreateInfo2 = {0};
		bufferCreateInfo2.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo2.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		bufferCreateInfo2.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo2, NULL, &accelBuffer);
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, accelBuffer, &memoryRequirements);
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((1 << i) & memoryRequirements.memoryTypeBits
				&& memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		vkBindBufferMemory(state->vkDevice, accelBuffer, memory, 0);
	}

	VkBuffer scratchBuffer;
	VkDeviceAddress scratchBufferAddress;
	{
		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = accelerationStructureBuildSizesInfo.buildScratchSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &scratchBuffer);

		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		
		VkMemoryRequirements memoryRequirements = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, scratchBuffer, &memoryRequirements);
		
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((1 << i) & memoryRequirements.memoryTypeBits
				&& memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);
		
		VkMemoryAllocateInfo memoryAllocateInfo = {0};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = i;
		
		VkDeviceMemory memory;
		vkAllocateMemory(state->vkDevice, &memoryAllocateInfo, NULL, &memory);
		vkBindBufferMemory(state->vkDevice, scratchBuffer, memory, 0);

		VkBufferDeviceAddressInfoKHR bufferDeviceAddressInfo = {0};
		bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		bufferDeviceAddressInfo.buffer = scratchBuffer;
		scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferDeviceAddressInfo);
	}
	
	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = accelBuffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	state->vkCreateAccelerationStructureKHR_(state->vkDevice, &accelerationStructureCreateInfo, NULL, &state->tlas);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {0};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.dstAccelerationStructure = state->tlas;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBufferAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {0};
	accelerationStructureBuildRangeInfo.primitiveCount = 1;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	VkAccelerationStructureBuildRangeInfoKHR *accelerationBuildStructureRangeInfos[] = {&accelerationStructureBuildRangeInfo};

	{
		VkCommandBufferBeginInfo beginInfo = {0};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
		state->vkCmdBuildAccelerationStructuresKHR_(state->vkCommandBuffer, 1,
			&accelerationBuildGeometryInfo, accelerationBuildStructureRangeInfos);
		vkEndCommandBuffer(state->vkCommandBuffer);

		vkResetFences(state->vkDevice, 1, &state->vkFence);

		VkSubmitInfo submitInfo = {0};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &state->vkCommandBuffer;
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

		vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	}

	//VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	//accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	//accelerationDeviceAddressInfo.accelerationStructure = topLevelAS.handle;
}


static void R_impl_create_raytrace_image(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	VkImageCreateInfo imageInfo = {0};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = state->width;
    imageInfo.extent.height = state->height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(state->vkDevice, &imageInfo, NULL, &state->vkRaytraceImage);

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(state->vkDevice, state->vkRaytraceImage, &memRequirements);

	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
	uint32_t i;
	for (i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((1 << i) & memRequirements.memoryTypeBits
			&& memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
			break;
		}
	}
	assert(i < memProperties.memoryTypeCount);

	VkMemoryAllocateInfo allocInfo = {0};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = i;

	VkDeviceMemory memory;
	vkAllocateMemory(state->vkDevice, &allocInfo, NULL, &memory);
	vkBindImageMemory(state->vkDevice, state->vkRaytraceImage, memory, 0);

	{
		VkCommandBufferBeginInfo beginInfo = {0};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
		
		VkImageMemoryBarrier barrier = {0};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = state->vkRaytraceImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier);
		
		vkEndCommandBuffer(state->vkCommandBuffer);

		vkResetFences(state->vkDevice, 1, &state->vkFence);

		VkSubmitInfo submitInfo = {0};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &state->vkCommandBuffer;
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

		vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	}
}


static void R_impl_create_raytrace_pipeline(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding = {0};
	accelerationStructureLayoutBinding.binding = 0;
	accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	accelerationStructureLayoutBinding.descriptorCount = 1;
	accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding resultImageLayoutBinding = {0};
	resultImageLayoutBinding.binding = 1;
	resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageLayoutBinding.descriptorCount = 1;
	resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	//VkDescriptorSetLayoutBinding uniformBufferBinding = {0};
	//uniformBufferBinding.binding = 2;
	//uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	//uniformBufferBinding.descriptorCount = 1;
	//uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	const VkDescriptorSetLayoutBinding bindings[] = {
		accelerationStructureLayoutBinding,
		resultImageLayoutBinding
	};

	VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI = {0};
	descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetlayoutCI.bindingCount = ARRAYSIZE(bindings);
	descriptorSetlayoutCI.pBindings = bindings;
	vkCreateDescriptorSetLayout(state->vkDevice, &descriptorSetlayoutCI, NULL, &state->vkDescriptorSetLayout);
	
	VkPipelineLayoutCreateInfo pipelineLayoutCI = {0};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &state->vkDescriptorSetLayout;
	vkCreatePipelineLayout(state->vkDevice, &pipelineLayoutCI, NULL, &state->vkPipelineLayout);

	VkPipelineShaderStageCreateInfo shaderStages[4] = {0};
	VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3] = {0};

	{
#include "raygen.h"

		VkShaderModuleCreateInfo createInfoVS = {0};
		createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfoVS.codeSize = raygen_spv_len;
		createInfoVS.pCode = (const uint32_t *)raygen_spv;
		VkShaderModule shaderModuleVS;
		vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

		VkPipelineShaderStageCreateInfo shaderStage = {0};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		shaderStages[0].module = shaderModuleVS;
		shaderStages[0].pName = "main";

		shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroups[0].generalShader = 0;
		shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
	}
	{
#include "closesthit.h"

		VkShaderModuleCreateInfo createInfoVS = {0};
		createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfoVS.codeSize = closesthit_spv_len;
		createInfoVS.pCode = (const uint32_t *)closesthit_spv;
		VkShaderModule shaderModuleVS;
		vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

		VkPipelineShaderStageCreateInfo shaderStage = {0};
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		shaderStages[1].module = shaderModuleVS;
		shaderStages[1].pName = "main";
		
		shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
		shaderGroups[1].generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[1].closestHitShader = 1;
		shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
	}
	{
#include "intersection.h"

		VkShaderModuleCreateInfo createInfoVS = {0};
		createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfoVS.codeSize = intersection_spv_len;
		createInfoVS.pCode = (const uint32_t *)intersection_spv;
		VkShaderModule shaderModuleVS;
		vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

		VkPipelineShaderStageCreateInfo shaderStage = {0};
		shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[2].stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		shaderStages[2].module = shaderModuleVS;
		shaderStages[2].pName = "main";
		
		//shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		//shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
		//shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
		//shaderGroups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
		//shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
		//shaderGroups[2].intersectionShader = 2;

		shaderGroups[1].intersectionShader = 2;
	}
	{
#include "miss.h"

		VkShaderModuleCreateInfo createInfoVS = {0};
		createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfoVS.codeSize = miss_spv_len;
		createInfoVS.pCode = (const uint32_t *)miss_spv;
		VkShaderModule shaderModuleVS;
		vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

		VkPipelineShaderStageCreateInfo shaderStage = {0};
		shaderStages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[3].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
		shaderStages[3].module = shaderModuleVS;
		shaderStages[3].pName = "main";
		
		shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
		shaderGroups[2].generalShader = 3;
		shaderGroups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;
	}

	VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI = {0};
	rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	rayTracingPipelineCI.stageCount = ARRAYSIZE(shaderStages);
	rayTracingPipelineCI.pStages = shaderStages;
	rayTracingPipelineCI.groupCount = ARRAYSIZE(shaderGroups);
	rayTracingPipelineCI.pGroups = shaderGroups;
	rayTracingPipelineCI.maxPipelineRayRecursionDepth = 31;
	rayTracingPipelineCI.layout = state->vkPipelineLayout;
	state->vkCreateRayTracingPipelinesKHR_(state->vkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, NULL, &state->vkPipeline);
}


static void R_impl_create_descriptors(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
	};
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {0};
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.pPoolSizes = poolSizes;
	descriptorPoolCreateInfo.poolSizeCount = ARRAYSIZE(poolSizes);
	descriptorPoolCreateInfo.maxSets = 1;
	vkCreateDescriptorPool(state->vkDevice, &descriptorPoolCreateInfo, NULL, &state->vkDescriptorPool);

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.descriptorPool = state->vkDescriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &state->vkDescriptorSetLayout;
	vkAllocateDescriptorSets(state->vkDevice, &descriptorSetAllocateInfo, &state->vkDescriptorSet);

	VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = {0};
	descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &state->tlas;

	VkWriteDescriptorSet accelerationStructureWrite = {0};
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
	accelerationStructureWrite.dstSet = state->vkDescriptorSet;
	accelerationStructureWrite.dstBinding = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	VkImageView imageView;
	VkImageViewCreateInfo imageViewCreateInfo = {0};
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	imageViewCreateInfo.image = state->vkRaytraceImage;
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vkCreateImageView(state->vkDevice, &imageViewCreateInfo, NULL, &imageView);

	VkDescriptorImageInfo storageImageDescriptor = {0};
	storageImageDescriptor.imageView = imageView;
	storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet resultImageWrite = {0};
	resultImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageWrite.descriptorCount = 1;
	resultImageWrite.pImageInfo = &storageImageDescriptor;
	resultImageWrite.dstSet = state->vkDescriptorSet;
	resultImageWrite.dstBinding = 1;

	const VkWriteDescriptorSet writeDescriptorSets[] = {
		accelerationStructureWrite, resultImageWrite
	};
	vkUpdateDescriptorSets(state->vkDevice, ARRAYSIZE(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
}


static void R_impl_create_binding_table(struct R_StateVK* state, const struct R_RendererDesc* desc)
{
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {0};
	rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	
	VkPhysicalDeviceProperties2 deviceProperties2 = {0};
	deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProperties2.pNext = &rayTracingPipelineProperties;
	vkGetPhysicalDeviceProperties2(state->vkPhysicalDevice, &deviceProperties2);

	const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
	const uint32_t handleSizeAligned = rayTracingPipelineProperties.shaderGroupHandleAlignment;
	const uint32_t groupCount = 3;
	const uint32_t sbtSize = groupCount * handleSizeAligned;

	uint8_t *shaderHandleStorage = _malloca(sbtSize);
	{
		state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 0, 1, sbtSize, shaderHandleStorage);

		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufferCreateInfo.size = handleSizeAligned;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &state->vkRaygenBuffer);
	
		VkMemoryAllocateFlagsInfoKHR allocFlagsInfo = {0};
		allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

		VkMemoryRequirements memRequirements;
		VkMemoryAllocateInfo memAlloc = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, state->vkRaygenBuffer, &memRequirements);
	
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if (((1 << i) & memRequirements.memoryTypeBits)
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) == (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
				memAlloc.memoryTypeIndex = i;
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);

		VkDeviceMemory memory;
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAlloc.allocationSize = memRequirements.size;
		memAlloc.pNext = &allocFlagsInfo;
		vkAllocateMemory(state->vkDevice, &memAlloc, NULL, &memory);

		// Copy handles
		void *mapped;
		vkMapMemory(state->vkDevice, memory, 0, handleSize, 0, &mapped);
		memcpy(mapped, shaderHandleStorage, handleSize);
		vkBindBufferMemory(state->vkDevice, state->vkRaygenBuffer, memory, 0);
	}
	{
		state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 1, 1, sbtSize, shaderHandleStorage);

		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufferCreateInfo.size = handleSizeAligned;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &state->vkClosestHitBuffer);
	
		VkMemoryAllocateFlagsInfoKHR allocFlagsInfo = {0};
		allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

		VkMemoryRequirements memRequirements;
		VkMemoryAllocateInfo memAlloc = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, state->vkClosestHitBuffer, &memRequirements);
	
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if (((1 << i) & memRequirements.memoryTypeBits)
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) == (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
				memAlloc.memoryTypeIndex = i;
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);

		VkDeviceMemory memory;
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAlloc.allocationSize = memRequirements.size;
		memAlloc.pNext = &allocFlagsInfo;
		vkAllocateMemory(state->vkDevice, &memAlloc, NULL, &memory);

		// Copy handles
		void *mapped;
		vkMapMemory(state->vkDevice, memory, 0, handleSize, 0, &mapped);
		memcpy(mapped, shaderHandleStorage, handleSize);
		vkBindBufferMemory(state->vkDevice, state->vkClosestHitBuffer, memory, 0);
	}
	{
		state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 2, 1, sbtSize, shaderHandleStorage);

		VkBufferCreateInfo bufferCreateInfo = {0};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		bufferCreateInfo.size = handleSizeAligned;
		vkCreateBuffer(state->vkDevice, &bufferCreateInfo, NULL, &state->vkMissBuffer);
	
		VkMemoryAllocateFlagsInfoKHR allocFlagsInfo = {0};
		allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

		VkMemoryRequirements memRequirements;
		VkMemoryAllocateInfo memAlloc = {0};
		vkGetBufferMemoryRequirements(state->vkDevice, state->vkMissBuffer, &memRequirements);
	
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		uint32_t i;
		for (i = 0; i < memProperties.memoryTypeCount; i++) {
			if (((1 << i) & memRequirements.memoryTypeBits)
				&& (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) == (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
				memAlloc.memoryTypeIndex = i;
				break;
			}
		}
		assert(i < memProperties.memoryTypeCount);

		VkDeviceMemory memory;
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAlloc.allocationSize = memRequirements.size;
		memAlloc.pNext = &allocFlagsInfo;
		vkAllocateMemory(state->vkDevice, &memAlloc, NULL, &memory);

		// Copy handles
		void *mapped;
		vkMapMemory(state->vkDevice, memory, 0, handleSize, 0, &mapped);
		memcpy(mapped, shaderHandleStorage, handleSize);
		vkBindBufferMemory(state->vkDevice, state->vkMissBuffer, memory, 0);
	}
	_freea(shaderHandleStorage);
}


static int R_impl_on_create(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	R_impl_create_instance(state, desc);
	R_impl_create_device(state, desc);
	R_impl_create_swapchain(state, desc);
	R_impl_create_blas(state, desc);
	R_impl_create_tlas(state, desc);
	R_impl_create_raytrace_image(state, desc);
	R_impl_create_raytrace_pipeline(state, desc);
	R_impl_create_descriptors(state, desc);
	R_impl_create_binding_table(state, desc);
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
		state->width = desc->width;
		state->height = desc->height;
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
		vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(state->vkDevice, state->vkFence, NULL);
		vkDestroySemaphore(state->vkDevice, state->vkImageAvailableSemaphore, NULL);
		vkDestroySemaphore(state->vkDevice, state->vkRenderFinishedSemaphore, NULL);
		vkDestroyRenderPass(state->vkDevice, state->vkRenderPass, NULL);
		vkDestroyPipelineLayout(state->vkDevice, state->vkPipelineLayout, NULL);
		vkDestroyPipeline(state->vkDevice, state->vkPipeline, NULL);
		for (size_t i = 0; i < R_SWAPCHAIN_COUNT; i++) {
			vkDestroyFramebuffer(state->vkDevice, state->vkSwapchainFramebuffers[i], NULL);
			vkDestroyImageView(state->vkDevice, state->vkSwapchainViews[i], NULL);
		}
		vkDestroyImage(state->vkDevice, state->vkRaytraceImage, NULL);
		vkDestroySwapchainKHR(state->vkDevice, state->vkSwapchain, NULL);
		vkDestroyCommandPool(state->vkDevice, state->vkCommandPool, NULL);
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
		CW_USEDEFAULT, 0, desc->width, desc->height, NULL, NULL, desc->hInstance, (LPVOID)desc);
	assert(hWnd);

	(void)ShowWindow(hWnd, desc->nCmdShow);
	(void)UpdateWindow(hWnd);

	*state = (struct R_State *)R_STATE(hWnd);
	return EXIT_SUCCESS;
}
