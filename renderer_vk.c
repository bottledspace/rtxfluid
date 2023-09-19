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

	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR_;
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

#if 0
	VkRenderPassBeginInfo renderPassInfo = {0};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = state->vkRenderPass;
	renderPassInfo.framebuffer = state->vkSwapchainFramebuffers[imageIndex];
	renderPassInfo.renderArea.offset.x = 0;
	renderPassInfo.renderArea.offset.y = 0;
	renderPassInfo.renderArea.extent.width = state->width;
	renderPassInfo.renderArea.extent.height = state->height;

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(state->vkCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(state->vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state->vkPipeline);

	VkViewport viewport = {0};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = state->width;
	viewport.height = state->height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(state->vkCommandBuffer, 0, 1, &viewport);
	
	VkRect2D scissor = {0};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = state->width;
	scissor.extent.height = state->height;
	vkCmdSetScissor(state->vkCommandBuffer, 0, 1, &scissor);

	vkCmdDraw(state->vkCommandBuffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(state->vkCommandBuffer);
#endif

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
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);

	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.image = state->vkRaytraceImage;
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
	
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.image = state->vkSwapchainImages[imageIndex];
	vkCmdPipelineBarrier(state->vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);
	
	vkEndCommandBuffer(state->vkCommandBuffer);

	const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
	VkSubmitInfo submitInfo = {0};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &state->vkImageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &state->vkCommandBuffer;

	VkSemaphore signalSemaphores[] = { state->vkRenderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	vkQueueSubmit(state->vkGraphicsQueue, 1, &submitInfo, state->vkFence);

	VkPresentInfoKHR presentInfo = {0};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

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

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
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
	state->vkCreateRayTracingPipelinesKHR_ = (PFN_vkCreateRayTracingPipelinesKHR)
		vkGetInstanceProcAddr(state->vkInstance, "vkCreateRayTracingPipelinesKHR");
	assert(state->vkCreateRayTracingPipelinesKHR_);
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

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingFeatures = {0};
	raytracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	raytracingFeatures.rayTracingPipeline = VK_TRUE;

	VkPhysicalDeviceFeatures deviceFeatures = {0};

	const char *extensionNames[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
	};

	VkDeviceCreateInfo deviceCreateInfo = {0};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = &raytracingFeatures;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
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

#if 0
static void R_impl_create_pipeline(struct R_StateVK *state, struct R_RendererDesc *desc)
{
#include "vert.h"
#include "frag.h"

	VkShaderModuleCreateInfo createInfoVS = {0};
	createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfoVS.codeSize = vert_spv_len;
	createInfoVS.pCode = (const uint32_t *)vert_spv;
	VkShaderModule shaderModuleVS;
	vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

	VkShaderModuleCreateInfo createInfoFS = {0};
	createInfoFS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfoFS.codeSize = frag_spv_len;
	createInfoFS.pCode = (const uint32_t *)frag_spv;
	VkShaderModule shaderModuleFS;
	vkCreateShaderModule(state->vkDevice, &createInfoFS, NULL, &shaderModuleFS);

	VkPipelineShaderStageCreateInfo shaderStageInfoVS = {0};
	shaderStageInfoVS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfoVS.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageInfoVS.module = shaderModuleVS;
	shaderStageInfoVS.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStageInfoFS = {0};
	shaderStageInfoFS.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfoFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageInfoFS.module = shaderModuleFS;
	shaderStageInfoFS.pName = "main";

	const VkPipelineShaderStageCreateInfo shaderStages[] = { shaderStageInfoVS, shaderStageInfoFS };
	const VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {0};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = ARRAYSIZE(dynamicStates);
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.pVertexBindingDescriptions = NULL;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;
	vertexInputInfo.pVertexAttributeDescriptions = NULL;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {0};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = state->width;
	viewport.height = state->height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {0};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = state->width;
	scissor.extent.height = state->height;

	VkPipelineViewportStateCreateInfo viewportState = {0};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {0};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {0};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = NULL;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlending = {0};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pSetLayouts = NULL;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = NULL;
	vkCreatePipelineLayout(state->vkDevice, &pipelineLayoutInfo, NULL, &state->vkPipelineLayout);

	VkGraphicsPipelineCreateInfo pipelineInfo = {0};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = state->vkPipelineLayout;
    pipelineInfo.renderPass = state->vkRenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	vkCreateGraphicsPipelines(state->vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &state->vkPipeline);

	vkDestroyShaderModule(state->vkDevice, shaderModuleFS, NULL);
    vkDestroyShaderModule(state->vkDevice, shaderModuleVS, NULL);
}
#endif

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

	VkMemoryAllocateInfo allocInfo = {0};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;

	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(state->vkPhysicalDevice, &memProperties);
	uint32_t i;
	for (i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((1 << i) & memRequirements.memoryTypeBits
			&& memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
			allocInfo.memoryTypeIndex = i;
			break;
		}
	}
	assert(i < memProperties.memoryTypeCount);

	VkDeviceMemory memory;
	vkAllocateMemory(state->vkDevice, &allocInfo, NULL, &memory);
	vkBindImageMemory(state->vkDevice, state->vkRaytraceImage, memory, 0);
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

	VkDescriptorSetLayoutBinding uniformBufferBinding = {0};
	uniformBufferBinding.binding = 2;
	uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformBufferBinding.descriptorCount = 1;
	uniformBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	const VkDescriptorSetLayoutBinding bindings[] = {
		resultImageLayoutBinding
	};

	VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI = {0};
	descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetlayoutCI.bindingCount = ARRAYSIZE(bindings);
	descriptorSetlayoutCI.pBindings = bindings;
	VkDescriptorSetLayout descriptorSetLayout;
	vkCreateDescriptorSetLayout(state->vkDevice, &descriptorSetlayoutCI, NULL, &descriptorSetLayout);
	
	VkPipelineLayoutCreateInfo pipelineLayoutCI = {0};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	vkCreatePipelineLayout(state->vkDevice, &pipelineLayoutCI, NULL, &pipelineLayout);

	VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
	VkRayTracingShaderGroupCreateInfoKHR shaderGroups[2] = {0};

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
#include "miss.h"

		VkShaderModuleCreateInfo createInfoVS = {0};
		createInfoVS.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfoVS.codeSize = miss_spv_len;
		createInfoVS.pCode = (const uint32_t *)miss_spv;
		VkShaderModule shaderModuleVS;
		vkCreateShaderModule(state->vkDevice, &createInfoVS, NULL, &shaderModuleVS);

		VkPipelineShaderStageCreateInfo shaderStage = {0};
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
		shaderStages[1].module = shaderModuleVS;
		shaderStages[1].pName = "main";
		
		shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroups[1].generalShader = 1;
		shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
	}

	VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI = {0};
	rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	rayTracingPipelineCI.stageCount = ARRAYSIZE(shaderStages);
	rayTracingPipelineCI.pStages = shaderStages;
	rayTracingPipelineCI.groupCount = ARRAYSIZE(shaderGroups);
	rayTracingPipelineCI.pGroups = shaderGroups;
	rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
	rayTracingPipelineCI.layout = pipelineLayout;
	state->vkCreateRayTracingPipelinesKHR_(state->vkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, NULL, &state->vkPipeline);
}


static int R_impl_on_create(struct R_StateVK *state, const struct R_RendererDesc *desc)
{
	R_impl_create_instance(state, desc);
	R_impl_create_device(state, desc);
	R_impl_create_swapchain(state, desc);
	//R_impl_create_pipeline(state, desc);
	R_impl_create_raytrace_image(state, desc);
	R_impl_create_raytrace_pipeline(state, desc);
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
