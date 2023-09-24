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
	vkBeginCommandBuffer(state->vkCommandBuffer,
		&(const VkCommandBufferBeginInfo) {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		});
	
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
	
	vkCmdPipelineBarrier(state->vkCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1,
		&(const VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkSwapchainImages[imageIndex],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		});
	vkCmdPipelineBarrier(state->vkCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1,
		&(const VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkRaytraceImage,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		});

	vkCmdCopyImage(state->vkCommandBuffer,
		state->vkRaytraceImage,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		state->vkSwapchainImages[imageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &(const VkImageCopy) {
			.extent.width = state->width,
			.extent.height = state->height,
			.extent.depth = 1,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.layerCount = 1,
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
		});

	vkCmdPipelineBarrier(state->vkCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1,
		&(const VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkSwapchainImages[imageIndex],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		});
	vkCmdPipelineBarrier(state->vkCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0, 0, NULL, 0, NULL, 1,
		&(const VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = state->vkRaytraceImage,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1
		});

	vkEndCommandBuffer(state->vkCommandBuffer);

	vkQueueSubmit(state->vkGraphicsQueue, 1,
		&(const VkSubmitInfo) {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &state->vkImageAvailableSemaphore,
			.pWaitDstStageMask = (const VkPipelineStageFlags[]){ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
			.commandBufferCount = 1,
			.pCommandBuffers = &state->vkCommandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &state->vkRenderFinishedSemaphore
		}, state->vkFence);
	vkQueuePresentKHR(state->vkGraphicsQueue,
		&(const VkPresentInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &state->vkRenderFinishedSemaphore,
			.swapchainCount = 1,
			.pSwapchains = (const VkSwapchainKHR[]) { state->vkSwapchain },
			.pImageIndices = &imageIndex
		});
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

	vkCreateInstance(
		&(const VkInstanceCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
			.enabledExtensionCount = ARRAYSIZE(extensionNames),
			.ppEnabledExtensionNames = extensionNames,
			.enabledLayerCount = ARRAYSIZE(validationLayerNames),
			.ppEnabledLayerNames = validationLayerNames,
		}, NULL, &state->vkInstance);
	
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
    state->vkCreateDebugUtilsMessengerEXT_ = (PFN_vkCreateDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateDebugUtilsMessengerEXT");
   
	VkDebugUtilsMessengerEXT messgenger;
    state->vkCreateDebugUtilsMessengerEXT_(state->vkInstance,
		&(const VkDebugUtilsMessengerCreateInfoEXT) {
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
		}, NULL, &messgenger);
}


static void R_impl_create_device(struct R_StateVK *state, struct R_RendererDesc *desc)
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, NULL);
	
	VkPhysicalDevice *physicalDevices = _malloca(sizeof(VkPhysicalDevice) * deviceCount);
	vkEnumeratePhysicalDevices(state->vkInstance, &deviceCount, physicalDevices);
	state->vkPhysicalDevice = physicalDevices[0];

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(state->vkPhysicalDevice, &queueFamilyCount, NULL);
	VkQueueFamilyProperties *queueFamilies = _malloca(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(state->vkPhysicalDevice, &queueFamilyCount, queueFamilies);

	uint32_t familyIndex;
	for (familyIndex = 0; familyIndex < queueFamilyCount; familyIndex++) {
		if (queueFamilies[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			break;
		}
	}
	assert(familyIndex < queueFamilyCount);

	const char *extensionNames[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME 
	};

	vkCreateDevice(state->vkPhysicalDevice, &(const VkDeviceCreateInfo) {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &(const VkPhysicalDeviceFeatures2) {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &(const VkPhysicalDeviceBufferDeviceAddressFeaturesKHR){
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
				.bufferDeviceAddress = VK_TRUE,
				.pNext = &(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR) {
					.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
					.rayTracingPipeline = VK_TRUE,
						.pNext = &(const VkPhysicalDeviceAccelerationStructureFeaturesKHR) {
							.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
							.accelerationStructure = VK_TRUE
						}
					}
				}
		},
		.pQueueCreateInfos = &(const VkDeviceQueueCreateInfo[]){{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = familyIndex,
			.queueCount = 1,
			.pQueuePriorities = (const float[]){ 1.0 },
			}},
		.queueCreateInfoCount = 1,
		.pEnabledFeatures = NULL,
		.enabledExtensionCount = ARRAYSIZE(extensionNames),
		.ppEnabledExtensionNames = extensionNames
	}, NULL, &state->vkDevice);
	vkGetDeviceQueue(state->vkDevice, familyIndex, 0, &state->vkGraphicsQueue);

	vkCreateCommandPool(state->vkDevice,
		&(const VkCommandPoolCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = familyIndex
		}, NULL, &state->vkCommandPool);

	vkAllocateCommandBuffers(state->vkDevice,
		&(const VkCommandBufferAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = state->vkCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		}, &state->vkCommandBuffer);

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


static void R_impl_create_buffer(struct R_StateVK *state, VkBuffer **buffer, VkDeviceSize size, const void *data,
	                             VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propFlags)
{
	vkCreateBuffer(state->vkDevice,
		&(const VkBufferCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.usage = usageFlags,
			.size = size,
		}, NULL, buffer);
		
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
	vkAllocateMemory(state->vkDevice,
		&(const VkMemoryAllocateInfo) {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = typeIndex,
			.pNext = &(const VkMemoryAllocateFlagsInfo) {
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
				.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
			}
		}, NULL, &memory);
	
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
	R_impl_create_buffer(state, &accelBuffer, accelerationStructureBuildSizesInfo.accelerationStructureSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo = {0};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = accelBuffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	state->vkCreateAccelerationStructureKHR_(state->vkDevice, &accelerationStructureCreateInfo, NULL, &state->blas);
	
	VkBuffer scratchBuffer;
	R_impl_create_buffer(state, &scratchBuffer, accelerationStructureBuildSizesInfo.buildScratchSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	const VkDeviceAddress scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice, &(const VkBufferDeviceAddressInfoKHR){
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = scratchBuffer
		});

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

	// TODO: Delete scratch buffer.
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
	R_impl_create_buffer(state, &instancesBuffer, sizeof(instance), &instance,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	const VkDeviceAddress instancesBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR){
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = instancesBuffer,
		});

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
	R_impl_create_buffer(state, &accelBuffer, accelerationStructureBuildSizesInfo.accelerationStructureSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkBuffer scratchBuffer;
	R_impl_create_buffer(state, &scratchBuffer, accelerationStructureBuildSizesInfo.buildScratchSize, NULL,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkDeviceAddress	scratchBufferAddress = state->vkGetBufferDeviceAddressKHR_(state->vkDevice,
		&(const VkBufferDeviceAddressInfoKHR){
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = scratchBuffer,
		});

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

	// TODO: Delete scratch buffer.
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

	state->rtHandleSize = R_IMPL_ALIGNTO(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);

	uint8_t *shaderHandleStorage = _malloca(3 * state->rtHandleSize);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 0, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkRaygenBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 1, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkClosestHitBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	state->vkGetRayTracingShaderGroupHandlesKHR_(state->vkDevice, state->vkPipeline, 2, 1, state->rtHandleSize, shaderHandleStorage);
	R_impl_create_buffer(state, &state->vkMissBuffer, state->rtHandleSize, shaderHandleStorage,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
