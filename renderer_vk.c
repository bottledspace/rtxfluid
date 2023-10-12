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

#define R_IMPL_ALIGNTO(value, align) ((align)*(((align)+(value)-1)/(align)))

struct R_Particle {
	float x, y, z;
	float radius;
};

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
	VkBuffer vkParticleBuffer;
	VkDescriptorSet vkDescriptorSet;
	VkDescriptorPool vkDescriptorPool;
	VkDescriptorSetLayout vkDescriptorSetLayout;
	VkBuffer vkRaygenBuffer;
	VkBuffer vkMissBuffer;
	VkBuffer vkClosestHitBuffer;
	VkAccelerationStructureKHR blas;
	VkAccelerationStructureKHR tlas;
	uint32_t rtHandleSize;

	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR_;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR_;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR_;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_;
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_;
};

static void R_impl_draw(struct R_StateVK *state)
{
	vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	vkResetFences(state->vkDevice, 1, &state->vkFence);

	uint32_t imageIndex;
    vkAcquireNextImageKHR(state->vkDevice, state->vkSwapchain, UINT64_MAX,
		state->vkImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	vkResetCommandBuffer(state->vkCommandBuffer, 0);
	{
		const VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
	}
	vkCmdBindPipeline(state->vkCommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, state->vkPipeline);
	vkCmdBindDescriptorSets(state->vkCommandBuffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		state->vkPipelineLayout, 0, 1, &state->vkDescriptorSet, 0, NULL);

	const VkDeviceAddress raygenAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = state->vkRaygenBuffer,
		});
	const VkDeviceAddress missAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = state->vkMissBuffer,
		});
	const VkDeviceAddress hitAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = state->vkClosestHitBuffer,
		});
	state->vkCmdTraceRaysKHR_(state->vkCommandBuffer,
		&(const VkStridedDeviceAddressRegionKHR) {
			.size = state->rtHandleSize,
			.stride = state->rtHandleSize,
			.deviceAddress = raygenAddress,
		},
		&(const VkStridedDeviceAddressRegionKHR) {
			.size = state->rtHandleSize,
			.stride = state->rtHandleSize,
			.deviceAddress = missAddress,
		},
		&(const VkStridedDeviceAddressRegionKHR) {
			.size = state->rtHandleSize,
			.stride = state->rtHandleSize,
			.deviceAddress = hitAddress,
		},
		&(const VkStridedDeviceAddressRegionKHR){0},
		state->width,
		state->height,
		1);
	{
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkSwapchainImages[imageIndex],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		};
		vkCmdPipelineBarrier(state->vkCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier);
	}
	{
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkRaytraceImage,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		};
		vkCmdPipelineBarrier(state->vkCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1,
			&barrier);
	}
	{
		const VkImageCopy imageCopy = {
			.extent.width = state->width,
			.extent.height = state->height,
			.extent.depth = 1,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.layerCount = 1,
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
		};
		vkCmdCopyImage(state->vkCommandBuffer,
			state->vkRaytraceImage,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			state->vkSwapchainImages[imageIndex],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &imageCopy);
	}
	{
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkSwapchainImages[imageIndex],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		};
		vkCmdPipelineBarrier(state->vkCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1,
			&barrier);
	}
	{
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkRaytraceImage,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		};
		vkCmdPipelineBarrier(state->vkCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1,
			&barrier);
	}

	vkEndCommandBuffer(state->vkCommandBuffer);
	{
		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &state->vkImageAvailableSemaphore,
			.pWaitDstStageMask = (const VkPipelineStageFlags[]){ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
			.commandBufferCount = 1,
			.pCommandBuffers = &state->vkCommandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &state->vkRenderFinishedSemaphore
		};
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);
	}
	{
		const VkPresentInfoKHR presentInfo = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &state->vkRenderFinishedSemaphore,
			.swapchainCount = 1,
			.pSwapchains = (const VkSwapchainKHR[]) { state->vkSwapchain },
			.pImageIndices = &imageIndex
		};
		vkQueuePresentKHR(state->vkGraphicsQueue, &presentInfo);
	}
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
	R_impl_draw((struct R_StateVK *)state_);
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


static void R_impl_create_instance(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	{
		const VkApplicationInfo appInfo = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = desc->title,
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = "No Engine",
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = VK_API_VERSION_1_3,
		};
		const char *extensionNames[] = {
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
			VK_KHR_SURFACE_EXTENSION_NAME
		};
		const char *validationLayerNames[] = {
			"VK_LAYER_KHRONOS_validation"
		};
		const VkInstanceCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
			.enabledExtensionCount = ARRAYSIZE(extensionNames),
			.ppEnabledExtensionNames = extensionNames,
			.enabledLayerCount = ARRAYSIZE(validationLayerNames),
			.ppEnabledLayerNames = validationLayerNames,
		};
		vkCreateInstance(&createInfo, NULL, &state->vkInstance);
	}

	state->vkCreateRayTracingPipelinesKHR_ = (PFN_vkCreateRayTracingPipelinesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateRayTracingPipelinesKHR");
	assert(state->vkCreateRayTracingPipelinesKHR_);
	state->vkCmdTraceRaysKHR_ = (PFN_vkCmdTraceRaysKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCmdTraceRaysKHR");
	assert(state->vkCmdTraceRaysKHR_);
	state->vkGetRayTracingShaderGroupHandlesKHR_ = (PFN_vkGetRayTracingShaderGroupHandlesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetRayTracingShaderGroupHandlesKHR");
	assert(state->vkGetRayTracingShaderGroupHandlesKHR_);
	state->vkGetBufferDeviceAddressKHR_ = (PFN_vkGetBufferDeviceAddressKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetBufferDeviceAddressKHR");
	assert(state->vkGetBufferDeviceAddressKHR_);
	state->vkCreateAccelerationStructureKHR_ = (PFN_vkCreateAccelerationStructureKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateAccelerationStructureKHR");
	assert(state->vkCreateAccelerationStructureKHR_);
	state->vkCmdBuildAccelerationStructuresKHR_ = (PFN_vkCmdBuildAccelerationStructuresKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCmdBuildAccelerationStructuresKHR");
	assert(state->vkCmdBuildAccelerationStructuresKHR_);
	state->vkGetAccelerationStructureBuildSizesKHR_ = (PFN_vkGetAccelerationStructureBuildSizesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetAccelerationStructureBuildSizesKHR");
	assert(state->vkGetAccelerationStructureBuildSizesKHR_);
	state->vkGetAccelerationStructureDeviceAddressKHR_ = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkGetAccelerationStructureDeviceAddressKHR");
	assert(state->vkGetAccelerationStructureDeviceAddressKHR_);
    state->vkCreateDebugUtilsMessengerEXT_ = (PFN_vkCreateDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateDebugUtilsMessengerEXT");
	assert(state->vkCreateDebugUtilsMessengerEXT_);
	
	{
		const VkDebugUtilsMessengerCreateInfoEXT createInfo = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.pNext = NULL,
			.flags = 0,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
			.pfnUserCallback = R_impl_print_debug_message,
			.pUserData = NULL
		};
		VkDebugUtilsMessengerEXT messgenger;
		state->vkCreateDebugUtilsMessengerEXT_(state->vkInstance, &createInfo, NULL, &messgenger);
	}
}


static void R_impl_create_device(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, NULL);
	
	VkPhysicalDevice *physicalDevices = _malloca(sizeof(VkPhysicalDevice) * deviceCount);
	assert(physicalDevices);
	vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, physicalDevices);
	
	// TODO: Search for correct physical device (one which supports RT)
	for (uint32_t i = 0; i < deviceCount; i++) {
		if (i == 0) {
			state->vkPhysicalDevice = physicalDevices[i];
		}
	}
	assert(state->vkPhysicalDevice);

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(state->vkPhysicalDevice, &queueFamilyCount, NULL);
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

	{
		const char *extensionNames[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME 
		};
		const VkDeviceQueueCreateInfo queueInfos[] = {{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = familyIndex,
			.queueCount = 1,
			.pQueuePriorities = (const float[]){ 1.0 },
		}};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
			.accelerationStructure = VK_TRUE
		};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipelineFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
			.rayTracingPipeline = VK_TRUE,
			.pNext = &accelFeatures
		};
		VkPhysicalDeviceBufferDeviceAddressFeaturesKHR addrFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
			.bufferDeviceAddress = VK_TRUE,
			.pNext = &pipelineFeatures
		};
		const VkPhysicalDeviceFeatures2 deviceFeatures2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &addrFeatures
		};
		const VkDeviceCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &deviceFeatures2,
			.pQueueCreateInfos = queueInfos,
			.queueCreateInfoCount = ARRAYSIZE(queueInfos),
			.pEnabledFeatures = NULL,
			.enabledExtensionCount = ARRAYSIZE(extensionNames),
			.ppEnabledExtensionNames = extensionNames
		};
		vkCreateDevice(state->vkPhysicalDevice, &createInfo, NULL, &state->vkDevice);
	}
	vkGetDeviceQueue(state->vkDevice, familyIndex, 0, &state->vkGraphicsQueue);
	
	{
		const VkCommandPoolCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = familyIndex
		};
		vkCreateCommandPool(state->vkDevice, &createInfo, NULL, &state->vkCommandPool);
	}
	{
		const VkCommandBufferAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = state->vkCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};
		vkAllocateCommandBuffers(state->vkDevice, &allocInfo, &state->vkCommandBuffer);
	}
	_freea(physicalDevices);
	_freea(queueFamilies);
}


static void R_impl_create_swapchain(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	{
		const VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
			.hwnd = state->hWnd,
			.hinstance = state->hInstance
		};
		vkCreateWin32SurfaceKHR(state->vkInstance, &surfaceCreateInfo, NULL, &state->vkSurface);
	}

	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkPhysicalDevice, state->vkSurface, &formatCount, NULL);
	VkSurfaceFormatKHR *surfaceFormats = _malloca(sizeof(VkSurfaceFormatKHR) * formatCount);
	assert(surfaceFormats);
	vkGetPhysicalDeviceSurfaceFormatsKHR(state->vkPhysicalDevice, state->vkSurface, &formatCount, surfaceFormats);

	uint32_t presentModeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(state->vkPhysicalDevice, state->vkSurface, &presentModeCount, NULL);
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
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state->vkPhysicalDevice, state->vkSurface, &surfaceCapabilities);

	state->width = surfaceCapabilities.currentExtent.width;
	state->height = surfaceCapabilities.currentExtent.height;
	{
		const VkSwapchainCreateInfoKHR swapchainCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = state->vkSurface,
			.minImageCount = R_SWAPCHAIN_COUNT,
			.imageFormat = surfaceFormats[formatIndex].format,
			.imageColorSpace = surfaceFormats[formatIndex].colorSpace,
			.imageExtent = surfaceCapabilities.currentExtent,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices = NULL,
			.preTransform = surfaceCapabilities.currentTransform,
			.compositeAlpha = surfaceCapabilities.supportedCompositeAlpha,
			.presentMode = presentModes[presentModeIndex],
			.clipped = VK_TRUE,
			.oldSwapchain = VK_NULL_HANDLE
		};
		vkCreateSwapchainKHR(state->vkDevice, &swapchainCreateInfo, NULL, &state->vkSwapchain);
	}

	uint32_t imageCount = R_SWAPCHAIN_COUNT;
	vkGetSwapchainImagesKHR(state->vkDevice, state->vkSwapchain, &imageCount, state->vkSwapchainImages);

	{
		const VkAttachmentDescription colorAttachment = {
			.format = surfaceFormats[formatIndex].format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		};
		const VkAttachmentReference colorAttachmentRef = {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		};
		const VkSubpassDescription subpass = {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentRef
		};
		const VkRenderPassCreateInfo renderPassInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &colorAttachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
		};
		vkCreateRenderPass(state->vkDevice, &renderPassInfo, NULL, &state->vkRenderPass);
	}
	for (size_t i = 0; i < R_SWAPCHAIN_COUNT; i++) {
		{
			const VkImageViewCreateInfo imageViewCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = state->vkSwapchainImages[i],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = surfaceFormats[formatIndex].format,
				.components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
				.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.subresourceRange.baseMipLevel = 0,
				.subresourceRange.levelCount = 1,
				.subresourceRange.baseArrayLayer = 0,
				.subresourceRange.layerCount = 1
			};
			vkCreateImageView(state->vkDevice, &imageViewCreateInfo, NULL, &state->vkSwapchainViews[i]);
		}
		{
			const VkFramebufferCreateInfo framebufferInfo = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = state->vkRenderPass,
				.attachmentCount = 1,
				.pAttachments = &state->vkSwapchainViews[i],
				.width = state->width,
				.height = state->height,
				.layers = 1,
			};
			vkCreateFramebuffer(state->vkDevice, &framebufferInfo, NULL, &state->vkSwapchainFramebuffers[i]);
		}
	}
	{
		const VkSemaphoreCreateInfo semaphoreCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};
		vkCreateSemaphore(state->vkDevice, &semaphoreCreateInfo, NULL, &state->vkRenderFinishedSemaphore);
		vkCreateSemaphore(state->vkDevice, &semaphoreCreateInfo, NULL, &state->vkImageAvailableSemaphore);
	}

	// Create the fence in a signalled state so we don't wait indefinitely
	// on the first frame.
	{
		const VkFenceCreateInfo fenceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};
		vkCreateFence(state->vkDevice, &fenceCreateInfo, NULL, &state->vkFence);
	}
	_freea(presentModes);
	_freea(surfaceFormats);
}


static void R_impl_create_buffer(struct R_StateVK *state, VkBuffer *buffer, VkDeviceSize size, const void *data,
	                             VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propFlags)
{
	const VkBufferCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.usage = usageFlags,
		.size = size,
	};
	vkCreateBuffer(state->vkDevice, &createInfo, NULL, buffer);
		
	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(state->vkDevice, *buffer, &memoryRequirements);

	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
		
	uint32_t typeIndex;
	for (typeIndex = 0; typeIndex < memProperties.memoryTypeCount; typeIndex++) {
		if (!((1 << typeIndex) & memoryRequirements.memoryTypeBits)) {
			continue;
		}
		if ((memProperties.memoryTypes[typeIndex].propertyFlags & propFlags) != propFlags) {
			continue;
		}
		break;
	}
	assert(typeIndex < memProperties.memoryTypeCount);
		
	VkDeviceMemory memory;
	{
		const VkMemoryAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = typeIndex,
			.pNext = &(const VkMemoryAllocateFlagsInfo) {
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
				.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
			}
		};
		vkAllocateMemory(state->vkDevice, &allocInfo, NULL, &memory);
	}
	if (data) {
		void *memoryPtr;
		vkMapMemory(state->vkDevice, memory, 0, size, 0, &memoryPtr);
		memcpy(memoryPtr, data, size);
		vkUnmapMemory(state->vkDevice, memory);
	}
	vkBindBufferMemory(state->vkDevice, *buffer, memory, 0);
}


static void R_impl_create_blas(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	VkAabbPositionsKHR aabb;
	aabb.minX = aabb.minY = aabb.minZ = -0.5;
	aabb.maxX = aabb.maxY = aabb.maxZ = 0.5;
	aabb.minZ += 2; aabb.maxZ += 2;

	VkBuffer aabbBuffer;
	R_impl_create_buffer(state, &aabbBuffer, sizeof(VkAabbPositionsKHR), &aabb,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	const VkBufferDeviceAddressInfoKHR bufferAddressInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
		.buffer = aabbBuffer
	};
	const VkAccelerationStructureGeometryKHR accelerationStructureGeometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.flags = 0,
		.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
		.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
		.geometry.aabbs.data.deviceAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &bufferAddressInfo),
		.geometry.aabbs.stride = sizeof(aabb),
	};

	const VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &accelerationStructureGeometry,
	};
	const uint32_t numPrims = 1;
	VkAccelerationStructureBuildSizesInfoKHR buildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
	};
	state->vkGetAccelerationStructureBuildSizesKHR_(state->vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&accelerationStructureBuildGeometryInfo, &numPrims, &buildInfo);

	VkBuffer accelBuffer;
	R_impl_create_buffer(state, &accelBuffer, buildInfo.accelerationStructureSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	const VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = accelBuffer,
		.size = buildInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};
	state->vkCreateAccelerationStructureKHR_(state->vkDevice, &accelerationStructureCreateInfo, NULL, &state->blas);
	
	VkBuffer scratchBuffer;
	R_impl_create_buffer(state, &scratchBuffer, buildInfo.buildScratchSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	const VkDeviceAddress scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR){
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = scratchBuffer
		});

	const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.dstAccelerationStructure = state->blas,
		.geometryCount = 1,
		.pGeometries = &accelerationStructureGeometry,
		.scratchData.deviceAddress = scratchBufferAddress,
	};
	const VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {
		.primitiveCount = 1,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	const VkAccelerationStructureBuildRangeInfoKHR *accelerationBuildStructureRangeInfos[] = {
		&accelerationStructureBuildRangeInfo
	};

	{
		const VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
		};
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
		
		state->vkCmdBuildAccelerationStructuresKHR_(state->vkCommandBuffer, 1,
			&accelerationBuildGeometryInfo, accelerationBuildStructureRangeInfos);
		
		vkEndCommandBuffer(state->vkCommandBuffer);

		vkResetFences(state->vkDevice, 1, &state->vkFence);

		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &state->vkCommandBuffer,
		};
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

		vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	}

	// TODO: Delete scratch buffer.
}


static void R_impl_create_tlas(struct R_StateVK* state, const struct R_RendererDesc* desc)
{
	const VkTransformMatrixKHR transformMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};
	const VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = state->blas
	};
	const VkDeviceAddress blasAddr = state->vkGetAccelerationStructureDeviceAddressKHR_(state->vkDevice, &accelerationDeviceAddressInfo);
	const VkAccelerationStructureInstanceKHR instance = {
		.transform = transformMatrix,
		.instanceCustomIndex = 0,
		.mask = 0xFF,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags = 0,
		.accelerationStructureReference = blasAddr,
	};
	VkBuffer instancesBuffer;
	R_impl_create_buffer(state, &instancesBuffer, sizeof(instance), &instance,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkDeviceAddress instancesBufferAddress;
	{
		const VkBufferDeviceAddressInfoKHR addrInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = instancesBuffer,
		};
		instancesBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &addrInfo);
	}

	

	VkAccelerationStructureBuildSizesInfoKHR buildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
	};
	const VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress = {
		.deviceAddress = instancesBufferAddress
	};
	const VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
		.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		.geometry.instances.arrayOfPointers = VK_FALSE,
		.geometry.instances.data = instanceDataDeviceAddress
	};
	{
		const VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			.geometryCount = 1,
			.pGeometries = &geometry
		};
		uint32_t count = 1;
		state->vkGetAccelerationStructureBuildSizesKHR_(state->vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo, &count, &buildInfo);
	}

	VkBuffer accelBuffer;
	R_impl_create_buffer(state, &accelBuffer, buildInfo.accelerationStructureSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkBuffer scratchBuffer;
	R_impl_create_buffer(state, &scratchBuffer, buildInfo.buildScratchSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkDeviceAddress	scratchBufferAddress;
	{
		const VkBufferDeviceAddressInfoKHR addrInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = scratchBuffer,
		};
		scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &addrInfo);
	}

	const VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = accelBuffer,
		.size = buildInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
	};
	state->vkCreateAccelerationStructureKHR_(state->vkDevice, &accelerationStructureCreateInfo, NULL, &state->tlas);

	{
		const VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
		};
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
	}
	{
		const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.dstAccelerationStructure = state->tlas,
			.geometryCount = 1,
			.pGeometries = &geometry,
			.scratchData.deviceAddress = scratchBufferAddress
		};
		const VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo = {
			.primitiveCount = 1,
			.primitiveOffset = 0,
			.firstVertex = 0,
			.transformOffset = 0
		};
		const VkAccelerationStructureBuildRangeInfoKHR *accelerationBuildStructureRangeInfos[] = {
			&accelerationStructureBuildRangeInfo
		};
		state->vkCmdBuildAccelerationStructuresKHR_(state->vkCommandBuffer, 1,
			&accelerationBuildGeometryInfo, accelerationBuildStructureRangeInfos);
	}
	vkEndCommandBuffer(state->vkCommandBuffer);
	vkResetFences(state->vkDevice, 1, &state->vkFence);
	{
		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &state->vkCommandBuffer
		};
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);
	}
	vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);

	// TODO: Delete scratch buffer.
}


static void R_impl_create_raytrace_image(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	{
		const VkImageCreateInfo imageInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.extent.width = state->width,
			.extent.height = state->height,
			.extent.depth = 1,
			.mipLevels = 1,
			.arrayLayers = 1,
			.format = VK_FORMAT_B8G8R8A8_UNORM,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		vkCreateImage(state->vkDevice, &imageInfo, NULL, &state->vkRaytraceImage);
	}
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
	
	VkDeviceMemory memory;
	{
		const VkMemoryAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = i
		};
		vkAllocateMemory(state->vkDevice, &allocInfo, NULL, &memory);
	}
	vkBindImageMemory(state->vkDevice, state->vkRaytraceImage, memory, 0);

	{
		const VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
		};
		vkBeginCommandBuffer(state->vkCommandBuffer, &beginInfo);
	}
	{
		const VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkRaytraceImage,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		};
		vkCmdPipelineBarrier(state->vkCommandBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier);
	}
	vkEndCommandBuffer(state->vkCommandBuffer);
	vkResetFences(state->vkDevice, 1, &state->vkFence);
	{
		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &state->vkCommandBuffer
		};
		vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);
	}
	vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
}


static void R_impl_create_raytrace_pipeline(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	{
		const VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding = {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
		};
		const VkDescriptorSetLayoutBinding resultImageLayoutBinding = {
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
		};
		const VkDescriptorSetLayoutBinding particlesLayoutBinding = {
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
		};
		const VkDescriptorSetLayoutBinding bindings[] = {
			accelerationStructureLayoutBinding,
			resultImageLayoutBinding,
			particlesLayoutBinding
		};
		const VkDescriptorSetLayoutCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = ARRAYSIZE(bindings),
			.pBindings = bindings
		};
		vkCreateDescriptorSetLayout(state->vkDevice, &createInfo, NULL, &state->vkDescriptorSetLayout);
	}
	{
		const VkPipelineLayoutCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &state->vkDescriptorSetLayout
		};
		vkCreatePipelineLayout(state->vkDevice, &createInfo, NULL, &state->vkPipelineLayout);
	}
	VkPipelineShaderStageCreateInfo shaderStages[4] = {0};
	VkRayTracingShaderGroupCreateInfoKHR shaderGroups[3] = {0};

	{
#include "raygen.h"

		const VkShaderModuleCreateInfo createInfoVS = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = raygen_spv_len,
			.pCode = (const uint32_t *)raygen_spv
		};
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

		const VkShaderModuleCreateInfo createInfoVS = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = closesthit_spv_len,
			.pCode = (const uint32_t *)closesthit_spv
		};
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

		const VkShaderModuleCreateInfo createInfoVS = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = intersection_spv_len,
			.pCode = (const uint32_t *)intersection_spv
		};
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

		const VkShaderModuleCreateInfo createInfoVS = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = miss_spv_len,
			.pCode = (const uint32_t *)miss_spv
		};
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

	const VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = ARRAYSIZE(shaderStages),
		.pStages = shaderStages,
		.groupCount = ARRAYSIZE(shaderGroups),
		.pGroups = shaderGroups,
		.maxPipelineRayRecursionDepth = 31,
		.layout = state->vkPipelineLayout
	};
	state->vkCreateRayTracingPipelinesKHR_(state->vkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, NULL, &state->vkPipeline);
}


static void R_impl_create_descriptors(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	{
		const VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
		};
		const VkDescriptorPoolCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.pPoolSizes = poolSizes,
			.poolSizeCount = ARRAYSIZE(poolSizes),
			.maxSets = 1
		};
		vkCreateDescriptorPool(state->vkDevice, &createInfo, NULL, &state->vkDescriptorPool);
	}
	{
		const VkDescriptorSetAllocateInfo allocInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = state->vkDescriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &state->vkDescriptorSetLayout
		};
		vkAllocateDescriptorSets(state->vkDevice, &allocInfo, &state->vkDescriptorSet);
	}
	VkImageView imageView;
	{
		const VkImageViewCreateInfo imageViewCreateInfo = {
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_B8G8R8A8_UNORM,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.levelCount = 1,
			.subresourceRange.layerCount = 1,
			.image = state->vkRaytraceImage,
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
		};
		vkCreateImageView(state->vkDevice, &imageViewCreateInfo, NULL, &imageView);
	}
	{
		const VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &state->tlas
		};
		const VkWriteDescriptorSet accelerationStructureWrite = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = &descriptorAccelerationStructureInfo,
			.dstSet = state->vkDescriptorSet,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
		};
		const VkDescriptorImageInfo storageImageDescriptor = {
			.imageView = imageView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};
		const VkWriteDescriptorSet resultImageWrite = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.pImageInfo = &storageImageDescriptor,
			.dstSet = state->vkDescriptorSet,
			.dstBinding = 1
		};
		const VkDescriptorBufferInfo storageBufferDescriptor = {
			.buffer = state->vkParticleBuffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE
		};
		const VkWriteDescriptorSet particlesBufferWrite = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &storageBufferDescriptor,
			.dstSet = state->vkDescriptorSet,
			.dstBinding = 2
		};
		const VkWriteDescriptorSet writeDescriptorSets[] = {
			accelerationStructureWrite,
			resultImageWrite,
			particlesBufferWrite
		};
		vkUpdateDescriptorSets(state->vkDevice, ARRAYSIZE(writeDescriptorSets), writeDescriptorSets, 0, VK_NULL_HANDLE);
	}
}


static void R_impl_create_binding_table(struct R_StateVK* state, const struct R_RendererDesc* desc)
{
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
	};
	{
		VkPhysicalDeviceProperties2 deviceProperties2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &rtProps,
		};
		vkGetPhysicalDeviceProperties2(state->vkPhysicalDevice, &deviceProperties2);
	}
	state->rtHandleSize = R_IMPL_ALIGNTO(rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment);

	uint8_t *shaderHandleStorage = _malloca(3 * state->rtHandleSize);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 0, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkRaygenBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 1, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkClosestHitBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 2, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkMissBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	_freea(shaderHandleStorage);
}


static int R_impl_on_create(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	R_impl_create_instance(state, desc);
	R_impl_create_device(state, desc);
	R_impl_create_swapchain(state, desc);

	struct R_Particle particle = {
		0.0,0.0,2.0,0.25
	};
	R_impl_create_buffer(state, &state->vkParticleBuffer, sizeof(struct R_Particle), &particle,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	R_impl_create_blas(state, desc);
	R_impl_create_tlas(state, desc);
	R_impl_create_raytrace_image(state, desc);
	R_impl_create_raytrace_pipeline(state, desc);
	R_impl_create_descriptors(state, desc);
	R_impl_create_binding_table(state, desc);
	return EXIT_SUCCESS;
}


static void R_impl_on_destroy(struct R_StateVK *state)
{
	// Wait for pending commands before we destroy resources.
	vkWaitForFences(state->vkDevice, 1, &state->vkFence, VK_TRUE, UINT64_MAX);
	vkDestroyFence(state->vkDevice, state->vkFence, NULL);

	vkDestroySemaphore(state->vkDevice, state->vkImageAvailableSemaphore, NULL);
	vkDestroySemaphore(state->vkDevice, state->vkRenderFinishedSemaphore, NULL);
	vkDestroyFence(state->vkDevice, state->vkFence, NULL);

	vkDestroyBuffer(state->vkDevice, state->vkMissBuffer, NULL);
	vkDestroyBuffer(state->vkDevice, state->vkClosestHitBuffer, NULL);
	vkDestroyBuffer(state->vkDevice, state->vkRaygenBuffer, NULL);

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
		R_impl_on_destroy(state);
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
	const WNDCLASSEX wc = {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = R_impl_WndProc,
		.cbClsExtra = 0,
		.cbWndExtra = 0,
		.hInstance = desc->hInstance,
		.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_APPLICATION)),
		.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)),
		.hbrBackground = NULL,
		.lpszMenuName = NULL,
		.lpszClassName = R_WNDCLASS,
		.hIconSm = wc.hIcon,
	};
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
