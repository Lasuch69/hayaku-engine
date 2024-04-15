#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include "shaders/material.gen.h"
#include "shaders/tonemap.gen.h"

#include "types/allocated.h"
#include "types/vertex.h"

#include "rendering_device.h"

vk::ShaderModule createShaderModule(vk::Device device, const uint32_t *pCode, size_t size) {
	vk::ShaderModuleCreateInfo createInfo =
			vk::ShaderModuleCreateInfo().setPCode(pCode).setCodeSize(size);

	return device.createShaderModule(createInfo);
}

void updateInputAttachment(vk::Device device, vk::ImageView imageView, vk::DescriptorSet dstSet) {
	vk::DescriptorImageInfo imageInfo =
			vk::DescriptorImageInfo()
					.setImageView(imageView)
					.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
					.setSampler(VK_NULL_HANDLE);

	vk::WriteDescriptorSet writeInfo =
			vk::WriteDescriptorSet()
					.setDstSet(dstSet)
					.setDstBinding(0)
					.setDstArrayElement(0)
					.setDescriptorType(vk::DescriptorType::eInputAttachment)
					.setDescriptorCount(1)
					.setImageInfo(imageInfo);

	device.updateDescriptorSets(writeInfo, nullptr);
}

vk::Pipeline createPipeline(vk::Device device, vk::ShaderModule vertexStage,
		vk::ShaderModule fragmentStage, vk::PipelineLayout pipelineLayout,
		vk::RenderPass renderPass, uint32_t subpass,
		vk::PipelineVertexInputStateCreateInfo vertexInput) {
	vk::PipelineShaderStageCreateInfo vertexStageInfo =
			vk::PipelineShaderStageCreateInfo()
					.setModule(vertexStage)
					.setStage(vk::ShaderStageFlagBits::eVertex)
					.setPName("main");

	vk::PipelineShaderStageCreateInfo fragmentStageInfo =
			vk::PipelineShaderStageCreateInfo()
					.setModule(fragmentStage)
					.setStage(vk::ShaderStageFlagBits::eFragment)
					.setPName("main");

	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly =
			vk::PipelineInputAssemblyStateCreateInfo().setTopology(
					vk::PrimitiveTopology::eTriangleList);

	vk::PipelineViewportStateCreateInfo viewportState =
			vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1);

	vk::PipelineRasterizationStateCreateInfo rasterizer =
			vk::PipelineRasterizationStateCreateInfo()
					.setDepthBiasEnable(VK_FALSE)
					.setRasterizerDiscardEnable(VK_FALSE)
					.setPolygonMode(vk::PolygonMode::eFill)
					.setLineWidth(1.0f)
					.setCullMode(vk::CullModeFlagBits::eBack)
					.setFrontFace(vk::FrontFace::eCounterClockwise)
					.setDepthBiasEnable(VK_FALSE);

	vk::PipelineMultisampleStateCreateInfo multisampling =
			vk::PipelineMultisampleStateCreateInfo()
					.setSampleShadingEnable(VK_FALSE)
					.setRasterizationSamples(vk::SampleCountFlagBits::e1);

	vk::PipelineDepthStencilStateCreateInfo depthStencil =
			vk::PipelineDepthStencilStateCreateInfo()
					.setDepthTestEnable(VK_TRUE)
					.setDepthWriteEnable(VK_TRUE)
					.setDepthCompareOp(vk::CompareOp::eLess)
					.setDepthBoundsTestEnable(VK_FALSE)
					.setStencilTestEnable(VK_FALSE);

	vk::PipelineColorBlendAttachmentState colorBlendAttachment =
			vk::PipelineColorBlendAttachmentState()
					.setColorWriteMask(
							vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
							vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
					.setBlendEnable(VK_FALSE);

	vk::PipelineColorBlendStateCreateInfo colorBlending =
			vk::PipelineColorBlendStateCreateInfo()
					.setLogicOpEnable(VK_FALSE)
					.setLogicOp(vk::LogicOp::eCopy)
					.setAttachments(colorBlendAttachment)
					.setBlendConstants({ 0.0f, 0.0f, 0.0f, 0.0f });

	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport,
		vk::DynamicState::eScissor };

	vk::PipelineDynamicStateCreateInfo dynamicState =
			vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);

	vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vk::GraphicsPipelineCreateInfo()
					.setStages(shaderStages)
					.setPVertexInputState(&vertexInput)
					.setPInputAssemblyState(&inputAssembly)
					.setPViewportState(&viewportState)
					.setPRasterizationState(&rasterizer)
					.setPMultisampleState(&multisampling)
					.setPDepthStencilState(&depthStencil)
					.setPColorBlendState(&colorBlending)
					.setPDynamicState(&dynamicState)
					.setLayout(pipelineLayout)
					.setRenderPass(renderPass)
					.setSubpass(subpass);

	vk::ResultValue<vk::Pipeline> result =
			device.createGraphicsPipeline(VK_NULL_HANDLE, pipelineCreateInfo);

	if (result.result != vk::Result::eSuccess)
		printf("Failed to create pipeline!\n");

	return result.value;
}

vk::CommandBuffer RenderingDevice::_beginSingleTimeCommands() {
	vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
													  .setLevel(vk::CommandBufferLevel::ePrimary)
													  .setCommandPool(_pContext->commandPool)
													  .setCommandBufferCount(1);

	vk::CommandBuffer commandBuffer = _pContext->device.allocateCommandBuffers(allocInfo)[0];

	vk::CommandBufferBeginInfo beginInfo = { vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
	commandBuffer.begin(beginInfo);

	return commandBuffer;
}

void RenderingDevice::_endSingleTimeCommands(vk::CommandBuffer commandBuffer) {
	commandBuffer.end();

	vk::SubmitInfo submitInfo =
			vk::SubmitInfo().setCommandBufferCount(1).setCommandBuffers(commandBuffer);

	_pContext->graphicsQueue.submit(submitInfo, VK_NULL_HANDLE);
	_pContext->graphicsQueue.waitIdle();

	_pContext->device.freeCommandBuffers(_pContext->commandPool, commandBuffer);
}

void RenderingDevice::_transitionImageLayout(vk::Image image, vk::Format format, uint32_t mipLevels,
		vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	vk::CommandBuffer commandBuffer = _beginSingleTimeCommands();

	vk::ImageSubresourceRange subresourceRange =
			vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(mipLevels)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

	vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
											 .setOldLayout(oldLayout)
											 .setNewLayout(newLayout)
											 .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
											 .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
											 .setImage(image)
											 .setSubresourceRange(subresourceRange);

	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	if (oldLayout == vk::ImageLayout::eUndefined &&
			newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier.setSrcAccessMask(vk::AccessFlagBits::eNone);
		barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eTransfer;
	} else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
			   newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
		barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);

		sourceStage = vk::PipelineStageFlagBits::eTransfer;
		destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
	} else {
		throw std::invalid_argument("Unsupported layout transition!");
	}

	commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, nullptr, nullptr, barrier);

	_endSingleTimeCommands(commandBuffer);
}

void RenderingDevice::_generateMipmaps(
		vk::Image image, int32_t width, int32_t height, vk::Format format, uint32_t mipLevels) {
	vk::FormatProperties properties = _pContext->physicalDevice.getFormatProperties(format);

	bool isBlittingSupported = (bool)(properties.optimalTilingFeatures &
									  vk::FormatFeatureFlagBits::eSampledImageFilterLinear);

	assert(isBlittingSupported);

	vk::CommandBuffer commandBuffer = _beginSingleTimeCommands();

	vk::ImageSubresourceRange subresourceRange =
			vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

	vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
											 .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
											 .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
											 .setImage(image);

	int32_t mipWidth = width;
	int32_t mipHeight = height;

	for (uint32_t i = 1; i < mipLevels; i++) {
		subresourceRange.setBaseMipLevel(i - 1);
		barrier.setSubresourceRange(subresourceRange);
		barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
		barrier.setNewLayout(vk::ImageLayout::eTransferSrcOptimal);
		barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
		barrier.setDstAccessMask(vk::AccessFlagBits::eTransferRead);

		commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

		std::array<vk::Offset3D, 2> srcOffsets = {
			vk::Offset3D(0, 0, 0),
			vk::Offset3D(mipWidth, mipHeight, 1),
		};

		std::array<vk::Offset3D, 2> dstOffsets = {
			vk::Offset3D(0, 0, 0),
			vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1),
		};

		vk::ImageSubresourceLayers srcSubresource =
				vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(i - 1)
						.setBaseArrayLayer(0)
						.setLayerCount(1);

		vk::ImageSubresourceLayers dstSubresource =
				vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(i)
						.setBaseArrayLayer(0)
						.setLayerCount(1);

		vk::ImageBlit blit = vk::ImageBlit()
									 .setSrcOffsets(srcOffsets)
									 .setDstOffsets(dstOffsets)
									 .setSrcSubresource(srcSubresource)
									 .setDstSubresource(dstSubresource);

		commandBuffer.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
				vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

		barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal);
		barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead);
		barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);

		commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

		if (mipWidth > 1)
			mipWidth /= 2;
		if (mipHeight > 1)
			mipHeight /= 2;
	}

	subresourceRange.setBaseMipLevel(mipLevels - 1);
	barrier.setSubresourceRange(subresourceRange);
	barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
	barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
	barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);

	commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

	_endSingleTimeCommands(commandBuffer);
}

AllocatedBuffer RenderingDevice::bufferCreate(
		vk::BufferUsageFlags usage, vk::DeviceSize size, VmaAllocationInfo *pAllocInfo) {
	VkBufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.size = size;
	createInfo.usage = static_cast<VkBufferUsageFlags>(usage);

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
							VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer buffer;
	VmaAllocation allocation;

	if (pAllocInfo == nullptr) {
		VmaAllocationInfo _allocInfo;
		vmaCreateBuffer(
				_allocator, &createInfo, &allocCreateInfo, &buffer, &allocation, &_allocInfo);
	} else {
		vmaCreateBuffer(
				_allocator, &createInfo, &allocCreateInfo, &buffer, &allocation, pAllocInfo);
	}

	return AllocatedBuffer{ allocation, buffer };
}

void RenderingDevice::bufferCopy(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
	vk::CommandBuffer commandBuffer = _beginSingleTimeCommands();

	vk::BufferCopy bufferCopy = vk::BufferCopy().setSrcOffset(0).setDstOffset(0).setSize(size);
	commandBuffer.copyBuffer(srcBuffer, dstBuffer, bufferCopy);

	_endSingleTimeCommands(commandBuffer);
}

void RenderingDevice::bufferSend(vk::Buffer dstBuffer, uint8_t *pData, size_t size) {
	VmaAllocationInfo stagingAllocInfo;
	AllocatedBuffer stagingBuffer =
			bufferCreate(vk::BufferUsageFlagBits::eTransferSrc, size, &stagingAllocInfo);

	memcpy(stagingAllocInfo.pMappedData, pData, size);
	vmaFlushAllocation(_allocator, stagingBuffer.allocation, 0, VK_WHOLE_SIZE);
	bufferCopy(stagingBuffer.buffer, dstBuffer, size);

	vmaDestroyBuffer(_allocator, stagingBuffer.buffer, stagingBuffer.allocation);
}

void RenderingDevice::bufferDestroy(AllocatedBuffer buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void RenderingDevice::_bufferCopyToImage(
		vk::Buffer srcBuffer, vk::Image dstImage, uint32_t width, uint32_t height) {
	vk::CommandBuffer commandBuffer = _beginSingleTimeCommands();

	vk::ImageSubresourceLayers imageSubresource =
			vk::ImageSubresourceLayers()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(0)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

	vk::BufferImageCopy region = vk::BufferImageCopy()
										 .setBufferOffset(0)
										 .setBufferRowLength(0)
										 .setBufferImageHeight(0)
										 .setImageSubresource(imageSubresource)
										 .setImageOffset(vk::Offset3D{ 0, 0, 0 })
										 .setImageExtent(vk::Extent3D{ width, height, 1 });

	commandBuffer.copyBufferToImage(
			srcBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, region);

	_endSingleTimeCommands(commandBuffer);
}

AllocatedImage RenderingDevice::imageCreate(uint32_t width, uint32_t height, vk::Format format,
		uint32_t mipmaps, vk::ImageUsageFlags usage) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipmaps;
	imageInfo.arrayLayers = 1;
	imageInfo.format = static_cast<VkFormat>(format);
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = static_cast<VkImageUsageFlags>(usage);
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	allocCreateInfo.priority = 1.0f;

	VmaAllocation allocation;
	VkImage image;
	vmaCreateImage(_allocator, &imageInfo, &allocCreateInfo, &image, &allocation, nullptr);

	return AllocatedImage{ allocation, image };
}

void RenderingDevice::imageDestroy(AllocatedImage image) {
	vmaDestroyImage(_allocator, image.image, image.allocation);
}

vk::ImageView RenderingDevice::imageViewCreate(
		vk::Image image, vk::Format format, uint32_t mipLevels) {
	vk::ImageSubresourceRange subresourceRange =
			vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(mipLevels)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

	vk::ImageViewCreateInfo createInfo = vk::ImageViewCreateInfo()
												 .setImage(image)
												 .setViewType(vk::ImageViewType::e2D)
												 .setFormat(format)
												 .setSubresourceRange(subresourceRange);

	return _pContext->device.createImageView(createInfo);
}

void RenderingDevice::imageViewDestroy(vk::ImageView imageView) {
	_pContext->device.destroyImageView(imageView);
}

vk::Sampler RenderingDevice::samplerCreate(
		vk::Filter filter, uint32_t mipLevels, float mipLodBias) {
	vk::PhysicalDeviceProperties properties = _pContext->physicalDevice.getProperties();
	float maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
	vk::SamplerAddressMode repeatMode = vk::SamplerAddressMode::eRepeat;

	vk::SamplerCreateInfo createInfo = vk::SamplerCreateInfo()
											   .setMagFilter(filter)
											   .setMinFilter(filter)
											   .setAddressModeU(repeatMode)
											   .setAddressModeV(repeatMode)
											   .setAddressModeW(repeatMode)
											   .setAnisotropyEnable(true)
											   .setMaxAnisotropy(maxAnisotropy)
											   .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
											   .setUnnormalizedCoordinates(false)
											   .setCompareEnable(false)
											   .setCompareOp(vk::CompareOp::eAlways)
											   .setMipmapMode(mipmapMode)
											   .setMinLod(0.0f)
											   .setMaxLod(static_cast<float>(mipLevels))
											   .setMipLodBias(mipLodBias);

	return _pContext->device.createSampler(createInfo);
}

void RenderingDevice::samplerDestroy(vk::Sampler sampler) {
	_pContext->device.destroySampler(sampler);
}

TextureRD RenderingDevice::textureCreate(Image *pImage) {
	uint32_t width = pImage->getWidth();
	uint32_t height = pImage->getHeight();

	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

	vk::Format format = vk::Format::eR8G8B8A8Unorm;

	AllocatedImage image = imageCreate(width, height, format, mipLevels,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
					vk::ImageUsageFlagBits::eSampled);

	const std::vector<uint8_t> &data = pImage->getData();

	VmaAllocationInfo stagingAllocInfo;
	AllocatedBuffer stagingBuffer =
			bufferCreate(vk::BufferUsageFlagBits::eTransferSrc, data.size(), &stagingAllocInfo);
	memcpy(stagingAllocInfo.pMappedData, data.data(), data.size());
	vmaFlushAllocation(_allocator, stagingBuffer.allocation, 0, VK_WHOLE_SIZE);

	_transitionImageLayout(image.image, format, mipLevels, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal);

	_bufferCopyToImage(stagingBuffer.buffer, image.image, width, height);

	bufferDestroy(stagingBuffer);

	// Transfers image layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	_generateMipmaps(image.image, width, height, format, mipLevels);

	vk::ImageView imageView = imageViewCreate(image.image, format, mipLevels);
	vk::Sampler sampler = samplerCreate(vk::Filter::eLinear, mipLevels);

	return {
		image,
		imageView,
		sampler,
	};
}

void RenderingDevice::updateUniformBuffer(const glm::mat4 &proj, const glm::mat4 &view,
		const glm::vec3 &viewPosition, uint32_t lightCount) {
	UniformBufferObject ubo{};
	ubo.projView = proj * view;
	ubo.viewPosition = viewPosition;
	ubo.lightCount = lightCount;

	memcpy(_uniformAllocInfos[_frame].pMappedData, &ubo, sizeof(ubo));
}

void RenderingDevice::updateLightBuffer(uint8_t *pData, size_t size) {
	bufferSend(_lightBuffer.buffer, pData, size);
}

vk::Instance RenderingDevice::getInstance() const {
	return _pContext->instance;
}

vk::Device RenderingDevice::getDevice() const {
	return _pContext->device;
}

vk::PipelineLayout RenderingDevice::getPipelineLayout() const {
	return _materialLayout;
}

vk::DescriptorPool RenderingDevice::getDescriptorPool() const {
	return _descriptorPool;
}

vk::DescriptorSetLayout RenderingDevice::getTextureLayout() const {
	return _textureLayout;
}

vk::CommandBuffer RenderingDevice::drawBegin() {
	vk::CommandBuffer commandBuffer = _commandBuffers[_frame];

	vk::Result result = _pContext->device.waitForFences(_fences[_frame], VK_TRUE, UINT64_MAX);

	if (result != vk::Result::eSuccess) {
		printf("Device failed to wait for fences!\n");
	}

	vk::ResultValue<uint32_t> image = _pContext->device.acquireNextImageKHR(
			_pContext->swapchain, UINT64_MAX, _presentSemaphores[_frame], VK_NULL_HANDLE);

	_imageIndex = image.value;

	if (image.result == vk::Result::eErrorOutOfDateKHR) {
		_pContext->recreateSwapchain(_width, _height);
		updateInputAttachment(_pContext->device, _pContext->colorView, _inputAttachmentSet);
	} else if (image.result != vk::Result::eSuccess && image.result != vk::Result::eSuboptimalKHR) {
		printf("Failed to acquire swapchain image!");
	}

	_pContext->device.resetFences(_fences[_frame]);

	commandBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo{};

	commandBuffer.begin(beginInfo);

	std::array<vk::ClearValue, 3> clearValues;
	clearValues[0] = vk::ClearValue();
	clearValues[1].color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
	clearValues[2].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

	vk::Extent2D extent = _pContext->swapchainExtent;

	vk::Viewport viewport = vk::Viewport()
									.setX(0.0f)
									.setY(0.0f)
									.setWidth(extent.width)
									.setHeight(extent.height)
									.setMinDepth(0.0f)
									.setMaxDepth(1.0f);

	vk::Rect2D scissor = vk::Rect2D().setOffset({ 0, 0 }).setExtent(extent);

	vk::RenderPassBeginInfo renderPassInfo =
			vk::RenderPassBeginInfo()
					.setRenderPass(_pContext->renderPass)
					.setFramebuffer(_pContext->swapchainImages[_imageIndex.value()].framebuffer)
					.setRenderArea(vk::Rect2D{ { 0, 0 }, extent })
					.setClearValues(clearValues);

	commandBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	commandBuffer.setViewport(0, viewport);
	commandBuffer.setScissor(0, scissor);

	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _materialPipeline);

	std::array<vk::DescriptorSet, 2> descriptorSets = { _uniformSets[_frame], _lightSet };
	commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics, _materialLayout, 0, descriptorSets, nullptr);

	return commandBuffer;
}

void RenderingDevice::drawEnd(vk::CommandBuffer commandBuffer) {
	if (!_imageIndex.has_value()) {
		throw std::runtime_error("Called drawEnd(), without calling drawBegin() first!");
	}

	commandBuffer.nextSubpass(vk::SubpassContents::eInline);

	// tonemapping

	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _tonemapPipeline);

	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _tonemapLayout, 0, 1,
			&_inputAttachmentSet, 0, nullptr);

	commandBuffer.draw(3, 1, 0, 0);

	// ImGui

	commandBuffer.endRenderPass();
	commandBuffer.end();

	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo = vk::SubmitInfo()
										.setWaitSemaphores(_presentSemaphores[_frame])
										.setWaitDstStageMask(waitStage)
										.setCommandBuffers(commandBuffer)
										.setSignalSemaphores(_renderSemaphores[_frame]);

	_pContext->graphicsQueue.submit(submitInfo, _fences[_frame]);

	vk::PresentInfoKHR presentInfo = vk::PresentInfoKHR()
											 .setWaitSemaphores(_renderSemaphores[_frame])
											 .setSwapchains(_pContext->swapchain)
											 .setImageIndices(_imageIndex.value());

	vk::Result err = _pContext->presentQueue.presentKHR(presentInfo);

	if (err == vk::Result::eErrorOutOfDateKHR || err == vk::Result::eSuboptimalKHR || _resized) {
		_pContext->recreateSwapchain(_width, _height);
		updateInputAttachment(_pContext->device, _pContext->colorView, _inputAttachmentSet);

		_resized = false;
	} else if (err != vk::Result::eSuccess) {
		printf("Failed to present swapchain image!\n");
	}

	_imageIndex.reset();
	_frame = (_frame + 1) % FRAMES_IN_FLIGHT;
}

void RenderingDevice::init(vk::SurfaceKHR surface, uint32_t width, uint32_t height) {
	_pContext->initialize(surface, width, height);

	// allocator

	VmaAllocatorCreateInfo allocatorCreateInfo = {};
	allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_1;
	allocatorCreateInfo.instance = _pContext->instance;
	allocatorCreateInfo.physicalDevice = _pContext->physicalDevice;
	allocatorCreateInfo.device = _pContext->device;

	{
		VkResult err = vmaCreateAllocator(&allocatorCreateInfo, &_allocator);

		if (err != VK_SUCCESS)
			printf("Failed to create allocator!\n");
	}

	// commands

	vk::Device device = _pContext->device;

	vk::CommandBufferAllocateInfo allocInfo = { _pContext->commandPool,
		vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(FRAMES_IN_FLIGHT) };

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		vk::Result err = device.allocateCommandBuffers(&allocInfo, _commandBuffers);

		if (err != vk::Result::eSuccess)
			printf("Failed to allocate command buffers!\n");
	}

	// sync

	vk::SemaphoreCreateInfo semaphoreInfo = {};
	vk::FenceCreateInfo fenceInfo = { vk::FenceCreateFlagBits::eSignaled };

	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		_presentSemaphores[i] = device.createSemaphore(semaphoreInfo);
		_renderSemaphores[i] = device.createSemaphore(semaphoreInfo);
		_fences[i] = device.createFence(fenceInfo);
	}

	// descriptor pool

	std::array<vk::DescriptorPoolSize, 4> poolSizes;
	poolSizes[0] = { vk::DescriptorType::eUniformBuffer, FRAMES_IN_FLIGHT };
	poolSizes[1] = { vk::DescriptorType::eInputAttachment, 1 };
	poolSizes[2] = { vk::DescriptorType::eStorageBuffer, 1 };
	poolSizes[3] = { vk::DescriptorType::eCombinedImageSampler, 1000 };

	uint32_t maxSets = 0;

	for (const vk::DescriptorPoolSize &poolSize : poolSizes) {
		maxSets += poolSize.descriptorCount;
	}

	vk::DescriptorPoolCreateInfo createInfo =
			vk::DescriptorPoolCreateInfo().setMaxSets(maxSets).setPoolSizes(poolSizes);

	_descriptorPool = device.createDescriptorPool(createInfo);

	// uniform

	{
		vk::DescriptorSetLayoutBinding binding =
				vk::DescriptorSetLayoutBinding()
						.setBinding(0)
						.setDescriptorType(vk::DescriptorType::eUniformBuffer)
						.setDescriptorCount(1)
						.setStageFlags(vk::ShaderStageFlagBits::eVertex |
									   vk::ShaderStageFlagBits::eFragment);

		vk::DescriptorSetLayoutCreateInfo createInfo =
				vk::DescriptorSetLayoutCreateInfo().setBindings(binding);

		vk::Result err = device.createDescriptorSetLayout(&createInfo, nullptr, &_uniformLayout);

		if (err != vk::Result::eSuccess) {
			printf("Failed to create uniform layout!\n");
		}

		std::vector<vk::DescriptorSetLayout> layouts(FRAMES_IN_FLIGHT, _uniformLayout);

		vk::DescriptorSetAllocateInfo allocInfo =
				vk::DescriptorSetAllocateInfo()
						.setDescriptorPool(_descriptorPool)
						.setDescriptorSetCount(static_cast<uint32_t>(FRAMES_IN_FLIGHT))
						.setSetLayouts(layouts);

		std::array<vk::DescriptorSet, FRAMES_IN_FLIGHT> uniformSets{};

		err = device.allocateDescriptorSets(&allocInfo, uniformSets.data());

		if (err != vk::Result::eSuccess) {
			printf("Failed to allocate uniform set!\n");
		}

		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			_uniformBuffers[i] = bufferCreate(
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
					sizeof(UniformBufferObject), &_uniformAllocInfos[i]);

			_uniformSets[i] = uniformSets[i];

			vk::DescriptorBufferInfo bufferInfo = vk::DescriptorBufferInfo()
														  .setBuffer(_uniformBuffers[i].buffer)
														  .setOffset(0)
														  .setRange(sizeof(UniformBufferObject));

			vk::WriteDescriptorSet writeInfo =
					vk::WriteDescriptorSet()
							.setDstSet(_uniformSets[i])
							.setDstBinding(0)
							.setDstArrayElement(0)
							.setDescriptorType(vk::DescriptorType::eUniformBuffer)
							.setDescriptorCount(1)
							.setBufferInfo(bufferInfo);

			device.updateDescriptorSets(writeInfo, nullptr);
		}
	}

	// input attachment

	{
		vk::DescriptorSetLayoutBinding binding =
				vk::DescriptorSetLayoutBinding()
						.setBinding(0)
						.setDescriptorType(vk::DescriptorType::eInputAttachment)
						.setDescriptorCount(1)
						.setStageFlags(vk::ShaderStageFlagBits::eFragment);

		vk::DescriptorSetLayoutCreateInfo createInfo =
				vk::DescriptorSetLayoutCreateInfo().setBindings(binding);

		vk::Result err =
				device.createDescriptorSetLayout(&createInfo, nullptr, &_inputAttachmentLayout);

		if (err != vk::Result::eSuccess) {
			printf("Failed to create image attachment layout!\n");
		}

		vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo()
														  .setDescriptorPool(_descriptorPool)
														  .setDescriptorSetCount(1)
														  .setSetLayouts(_inputAttachmentLayout);

		err = device.allocateDescriptorSets(&allocInfo, &_inputAttachmentSet);

		if (err != vk::Result::eSuccess) {
			printf("Failed to allocate input attachment set!\n");
		}

		updateInputAttachment(device, _pContext->colorView, _inputAttachmentSet);
	}

	// light

	{
		vk::DescriptorSetLayoutBinding binding =
				vk::DescriptorSetLayoutBinding()
						.setBinding(0)
						.setDescriptorType(vk::DescriptorType::eStorageBuffer)
						.setDescriptorCount(1)
						.setStageFlags(vk::ShaderStageFlagBits::eFragment);

		vk::DescriptorSetLayoutCreateInfo createInfo =
				vk::DescriptorSetLayoutCreateInfo().setBindings(binding);

		vk::Result err = device.createDescriptorSetLayout(&createInfo, nullptr, &_lightLayout);

		if (err != vk::Result::eSuccess) {
			printf("Failed to create light layout!\n");
		}

		vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo()
														  .setDescriptorPool(_descriptorPool)
														  .setDescriptorSetCount(1)
														  .setSetLayouts(_lightLayout);

		err = device.allocateDescriptorSets(&allocInfo, &_lightSet);

		if (err != vk::Result::eSuccess) {
			printf("Failed to allocate light set!\n");
		}

		vk::DeviceSize bufferSize = sizeof(PointLightRD) * MAX_LIGHT_COUNT;

		_lightBuffer = bufferCreate(
				vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
				bufferSize);

		vk::DescriptorBufferInfo bufferInfo = vk::DescriptorBufferInfo()
													  .setBuffer(_lightBuffer.buffer)
													  .setOffset(0)
													  .setRange(bufferSize);

		vk::WriteDescriptorSet writeInfo =
				vk::WriteDescriptorSet()
						.setDstSet(_lightSet)
						.setDstBinding(0)
						.setDstArrayElement(0)
						.setDescriptorType(vk::DescriptorType::eStorageBuffer)
						.setDescriptorCount(1)
						.setBufferInfo(bufferInfo);

		device.updateDescriptorSets(writeInfo, nullptr);
	}

	// textures

	{
		vk::DescriptorSetLayoutBinding binding =
				vk::DescriptorSetLayoutBinding()
						.setBinding(0)
						.setDescriptorCount(1)
						.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
						.setStageFlags(vk::ShaderStageFlagBits::eFragment);

		vk::DescriptorSetLayoutCreateInfo createInfo =
				vk::DescriptorSetLayoutCreateInfo().setBindings(binding);

		vk::Result err = device.createDescriptorSetLayout(&createInfo, nullptr, &_textureLayout);

		if (err != vk::Result::eSuccess) {
			printf("Failed to create texture layout!\n");
		}
	}

	// material

	{
		MaterialShader shader;

		vk::ShaderModule vertexStage =
				createShaderModule(device, shader.vertexCode, sizeof(shader.vertexCode));

		vk::ShaderModule fragmentStage =
				createShaderModule(device, shader.fragmentCode, sizeof(shader.fragmentCode));

		std::array<vk::DescriptorSetLayout, 3> layouts = {
			_uniformLayout,
			_lightLayout,
			_textureLayout,
		};

		vk::PushConstantRange pushConstant = vk::PushConstantRange(
				vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshPushConstants));

		vk::PipelineLayoutCreateInfo createInfo =
				vk::PipelineLayoutCreateInfo().setSetLayouts(layouts).setPushConstantRanges(
						pushConstant);

		vk::VertexInputBindingDescription binding = Vertex::getBindingDescription();
		std::array<vk::VertexInputAttributeDescription, 4> attribute =
				Vertex::getAttributeDescriptions();

		vk::PipelineVertexInputStateCreateInfo vertexInput =
				vk::PipelineVertexInputStateCreateInfo()
						.setVertexBindingDescriptions(binding)
						.setVertexAttributeDescriptions(attribute);

		_materialLayout = device.createPipelineLayout(createInfo);
		_materialPipeline = createPipeline(device, vertexStage, fragmentStage, _materialLayout,
				_pContext->renderPass, 0, vertexInput);

		device.destroyShaderModule(vertexStage);
		device.destroyShaderModule(fragmentStage);
	}

	// tonemapping

	{
		TonemapShader shader;

		vk::ShaderModule vertexStage =
				createShaderModule(device, shader.vertexCode, sizeof(shader.vertexCode));

		vk::ShaderModule fragmentStage =
				createShaderModule(device, shader.fragmentCode, sizeof(shader.fragmentCode));

		vk::PipelineLayoutCreateInfo createInfo =
				vk::PipelineLayoutCreateInfo().setSetLayouts(_inputAttachmentLayout);

		_tonemapLayout = device.createPipelineLayout(createInfo);
		_tonemapPipeline = createPipeline(
				device, vertexStage, fragmentStage, _tonemapLayout, _pContext->renderPass, 1, {});

		device.destroyShaderModule(vertexStage);
		device.destroyShaderModule(fragmentStage);
	}
}

void RenderingDevice::windowResize(uint32_t width, uint32_t height) {
	if (_width == width && _height == height) {
		return;
	}

	_width = width;
	_height = height;
	_resized = true;
}

RenderingDevice::RenderingDevice(std::vector<const char *> extensions, bool useValidation) {
	_pContext = new VulkanContext(extensions, useValidation);
}
