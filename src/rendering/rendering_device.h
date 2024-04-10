#ifndef RENDERING_DEVICE_H
#define RENDERING_DEVICE_H

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

#include "types/allocated.h"

#include "../image.h"

#include "vulkan_context.h"

const int FRAMES_IN_FLIGHT = 2;
const int MAX_LIGHT_COUNT = 8;

struct UniformBufferObject {
	glm::mat4 projView;
	glm::mat4 view;
	uint32_t lightCount;
};

struct MeshPushConstants {
	glm::mat4 model;
	glm::mat4 modelViewNormal;
};

struct LightData {
	glm::vec3 position;
	float range;
	glm::vec3 color;
	float intensity;
};

struct Texture {
	AllocatedImage image;
	vk::ImageView imageView;
	vk::Sampler sampler;
};

class RenderingDevice {
private:
	VulkanContext *_pContext;
	uint32_t _frame = 0;

	uint32_t _width, _height;
	bool _resized;

	VmaAllocator _allocator;
	vk::CommandBuffer _commandBuffers[FRAMES_IN_FLIGHT];

	vk::Semaphore _presentSemaphores[FRAMES_IN_FLIGHT];
	vk::Semaphore _renderSemaphores[FRAMES_IN_FLIGHT];
	vk::Fence _fences[FRAMES_IN_FLIGHT];

	vk::DescriptorPool _descriptorPool;

	vk::DescriptorSetLayout _uniformLayout;
	vk::DescriptorSetLayout _inputAttachmentLayout;
	vk::DescriptorSetLayout _lightLayout;
	vk::DescriptorSetLayout _textureLayout;

	vk::DescriptorSet _uniformSets[FRAMES_IN_FLIGHT];
	vk::DescriptorSet _inputAttachmentSet;
	vk::DescriptorSet _lightSet;

	AllocatedBuffer _uniformBuffers[FRAMES_IN_FLIGHT];
	VmaAllocationInfo _uniformAllocInfos[FRAMES_IN_FLIGHT];
	AllocatedBuffer _lightBuffer;

	vk::PipelineLayout _materialLayout;
	vk::Pipeline _materialPipeline;

	vk::PipelineLayout _tonemapLayout;
	vk::Pipeline _tonemapPipeline;

	std::optional<uint32_t> _imageIndex;

	vk::CommandBuffer _beginSingleTimeCommands();
	void _endSingleTimeCommands(vk::CommandBuffer commandBuffer);

	void _transitionImageLayout(vk::Image image, vk::Format format, uint32_t mipLevels,
			vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

	void _bufferCopyToImage(
			vk::Buffer srcBuffer, vk::Image dstImage, uint32_t width, uint32_t height);

	void _generateMipmaps(
			vk::Image image, int32_t width, int32_t height, vk::Format format, uint32_t mipLevels);

public:
	AllocatedBuffer bufferCreate(
			vk::BufferUsageFlags usage, vk::DeviceSize size, VmaAllocationInfo *pAllocInfo = NULL);
	void bufferCopy(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);
	void bufferSend(vk::Buffer dstBuffer, uint8_t *pData, size_t size);
	void bufferDestroy(AllocatedBuffer buffer);

	AllocatedImage imageCreate(uint32_t width, uint32_t height, vk::Format format, uint32_t mipmaps,
			vk::ImageUsageFlags usage);
	void imageDestroy(AllocatedImage image);

	vk::ImageView imageViewCreate(vk::Image image, vk::Format format, uint32_t mipLevels);
	void imageViewDestroy(vk::ImageView imageView);

	vk::Sampler samplerCreate(vk::Filter filter, uint32_t mipLevels, float mipLodBias = 0.0f);
	void samplerDestroy(vk::Sampler sampler);

	Texture textureCreate(Image *pImage);

	void updateUniformBuffer(const glm::mat4 &proj, const glm::mat4 &view, uint32_t lightCount);
	void updateLightBuffer(uint8_t *pData, size_t size);

	vk::Instance getInstance() const;
	vk::Device getDevice() const;

	vk::PipelineLayout getPipelineLayout() const;

	vk::DescriptorPool getDescriptorPool() const;
	vk::DescriptorSetLayout getTextureLayout() const;

	vk::CommandBuffer drawBegin();
	void drawEnd(vk::CommandBuffer commandBuffer);

	void init(vk::SurfaceKHR surface, uint32_t width, uint32_t height);
	void windowResize(uint32_t width, uint32_t height);

	RenderingDevice(std::vector<const char *> extensions, bool useValidation);
};

#endif // !RENDERING_DEVICE_H
