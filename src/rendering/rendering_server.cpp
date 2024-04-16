#include <cassert>
#include <cstdint>
#include <iostream>

#include <glm/glm.hpp>
#include <optional>

#include "rendering_device.h"
#include "rendering_server.h"

#define CHECK_IF_VALID(owner, id, what)                                                            \
	if (!owner.has(id)) {                                                                          \
		std::cout << "ERROR: " << what << ": " << id << " is not valid resource!" << std::endl;    \
		return;                                                                                    \
	}

void RenderingServer::_updateLights() {
	if (_pointLights.size() == 0) {
		PointLightRD lightData{};
		_pDevice->updateLightBuffer((uint8_t *)&lightData, sizeof(PointLightRD));
		return;
	}

	std::vector<PointLightRD> lights;
	for (const auto &[id, light] : _pointLights.map()) {
		if (lights.size() >= 8)
			break;

		lights.push_back(light);
	}

	_pDevice->updateLightBuffer((uint8_t *)lights.data(), sizeof(PointLightRD) * lights.size());
}

void RS::cameraSetTransform(const glm::mat4 &transform) {
	_camera.transform = transform;
}

void RS::cameraSetFovY(float fovY) {
	_camera.fovY = fovY;
}

void RS::cameraSetZNear(float zNear) {
	_camera.zNear = zNear;
}

void RS::cameraSetZFar(float zFar) {
	_camera.zFar = zFar;
}

Mesh RS::meshCreate() {
	return _meshes.insert({});
}

void RS::meshAddPrimitive(Mesh mesh, const std::vector<Vertex> &vertices,
		const std::vector<uint32_t> &indices, Material material) {
	CHECK_IF_VALID(_meshes, mesh, "Mesh");
	CHECK_IF_VALID(_materials, material, "Material");

	vk::DeviceSize vertexSize = sizeof(Vertex) * vertices.size();
	AllocatedBuffer vertexBuffer = _pDevice->bufferCreate(
			vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vertexSize);
	_pDevice->bufferSend(vertexBuffer.buffer, (uint8_t *)vertices.data(), (size_t)vertexSize);

	vk::DeviceSize indexSize = sizeof(uint32_t) * indices.size();
	AllocatedBuffer indexBuffer = _pDevice->bufferCreate(
			vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			indexSize);
	_pDevice->bufferSend(indexBuffer.buffer, (uint8_t *)indices.data(), (size_t)indexSize);

	PrimitiveRD primitive = {
		vertexBuffer,
		indexBuffer,
		static_cast<uint32_t>(indices.size()),
		material,
	};

	_meshes[mesh].primitives.push_back(primitive);
}

void RS::meshFree(Mesh mesh) {
	std::optional<MeshRD> result = _meshes.remove(mesh);

	if (!result.has_value())
		return;

	MeshRD _mesh = result.value();
	for (PrimitiveRD primitive : _mesh.primitives) {
		_pDevice->bufferDestroy(primitive.vertexBuffer);
		_pDevice->bufferDestroy(primitive.indexBuffer);
	}
}

MeshInstance RenderingServer::meshInstanceCreate() {
	return _meshInstances.insert({});
}

void RS::meshInstanceSetMesh(MeshInstance meshInstance, Mesh mesh) {
	CHECK_IF_VALID(_meshInstances, meshInstance, "MeshInstance");
	CHECK_IF_VALID(_meshes, mesh, "Mesh")

	_meshInstances[meshInstance].mesh = mesh;
}

void RS::meshInstanceSetTransform(MeshInstance meshInstance, const glm::mat4 &transform) {
	CHECK_IF_VALID(_meshInstances, meshInstance, "MeshInstance");

	_meshInstances[meshInstance].transform = transform;
}

void RS::meshInstanceFree(MeshInstance meshInstance) {
	_meshInstances.remove(meshInstance);
}

PointLight RenderingServer::pointLightCreate() {
	PointLight pointLight = _pointLights.insert({});
	_updateLights();

	return pointLight;
}

void RS::pointLightSetPosition(PointLight pointLight, const glm::vec3 &position) {
	CHECK_IF_VALID(_pointLights, pointLight, "PointLight");

	_pointLights[pointLight].position = position;
	_updateLights();
}

void RS::pointLightSetRange(PointLight pointLight, float range) {
	CHECK_IF_VALID(_pointLights, pointLight, "PointLight");

	_pointLights[pointLight].range = range;
	_updateLights();
}

void RS::pointLightSetColor(PointLight pointLight, const glm::vec3 &color) {
	CHECK_IF_VALID(_pointLights, pointLight, "PointLight");

	_pointLights[pointLight].color = color;
	_updateLights();
}

void RS::pointLightSetIntensity(PointLight pointLight, float intensity) {
	CHECK_IF_VALID(_pointLights, pointLight, "PointLight");

	_pointLights[pointLight].intensity = intensity;
	_updateLights();
}

void RS::pointLightFree(PointLight pointLight) {
	std::optional<PointLightRD> result = _pointLights.remove(pointLight);

	if (!result.has_value())
		return;

	_updateLights();
}

Texture RS::textureCreate(Image *pImage) {
	if (pImage == nullptr)
		return NULL_HANDLE;

	if (pImage->getFormat() != Image::Format::RGBA8) {
		std::cout << "Image format RGBA8 is required to create texture!" << std::endl;
		return NULL_HANDLE;
	}

	TextureRD _texture = _pDevice->textureCreate(pImage);
	return _textures.insert(_texture);
}

void RS::textureFree(Texture texture) {
	std::optional<TextureRD> result = _textures.remove(texture);

	if (!result.has_value())
		return;

	TextureRD _texture = result.value();
	_pDevice->imageDestroy(_texture.image);
	_pDevice->imageViewDestroy(_texture.imageView);
	_pDevice->samplerDestroy(_texture.sampler);
}

Material RS::materialCreate(Texture albedoTexture) {
	if (!_textures.has(albedoTexture))
		albedoTexture = _whiteTexture;

	TextureRD _albedoTexture = _textures[albedoTexture];

	vk::DescriptorSetLayout textureLayout = _pDevice->getTextureLayout();

	vk::DescriptorSetAllocateInfo allocInfo =
			vk::DescriptorSetAllocateInfo()
					.setDescriptorPool(_pDevice->getDescriptorPool())
					.setDescriptorSetCount(1)
					.setSetLayouts(textureLayout);

	VkDescriptorSet albedoSet = _pDevice->getDevice().allocateDescriptorSets(allocInfo)[0];

	vk::DescriptorImageInfo imageInfo =
			vk::DescriptorImageInfo()
					.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
					.setImageView(_albedoTexture.imageView)
					.setSampler(_albedoTexture.sampler);

	vk::WriteDescriptorSet writeInfo =
			vk::WriteDescriptorSet()
					.setDstSet(albedoSet)
					.setDstBinding(0)
					.setDstArrayElement(0)
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
					.setDescriptorCount(1)
					.setImageInfo(imageInfo);

	_pDevice->getDevice().updateDescriptorSets(writeInfo, nullptr);

	return _materials.insert({ albedoSet });
}

void RS::materialFree(Material material) {
	_materials.remove(material);
}

void RenderingServer::draw() {
	_pDevice->updateUniformBuffer(_camera.transform[3], _pointLights.size());

	float aspect = static_cast<float>(_width) / static_cast<float>(_height);
	glm::mat4 projView = _camera.projectionMatrix(aspect) * _camera.viewMatrix();

	vk::CommandBuffer commandBuffer = _pDevice->drawBegin();

	for (const auto &[meshInstance, meshInstanceRS] : _meshInstances.map()) {
		const MeshRD &mesh = _meshes[meshInstanceRS.mesh];

		MeshPushConstants constants{};
		constants.projView = projView;
		constants.model = meshInstanceRS.transform;

		commandBuffer.pushConstants(_pDevice->getPipelineLayout(), vk::ShaderStageFlagBits::eVertex,
				0, sizeof(MeshPushConstants), &constants);

		for (const PrimitiveRD &primitive : mesh.primitives) {
			MaterialRD material = _materials[primitive.material];

			commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
					_pDevice->getPipelineLayout(), 2, material.albedoSet, nullptr);

			vk::DeviceSize offset = 0;
			commandBuffer.bindVertexBuffers(0, 1, &primitive.vertexBuffer.buffer, &offset);
			commandBuffer.bindIndexBuffer(primitive.indexBuffer.buffer, 0, vk::IndexType::eUint32);
			commandBuffer.drawIndexed(primitive.indexCount, 1, 0, 0, 0);
		}
	}

	_pDevice->drawEnd(commandBuffer);
}

void RS::init(const std::vector<const char *> &extensions) {
	_pDevice = new RenderingDevice(extensions, _useValidation);
}

vk::Instance RS::getVkInstance() const {
	return _pDevice->getInstance();
}

void RS::windowInit(vk::SurfaceKHR surface, uint32_t width, uint32_t height) {
	_pDevice->init(surface, width, height);

	_width = width;
	_height = height;

	Image *pImage = Image::create(1, 1, Image::Format::RGBA8, { 255, 255, 255, 255 });

	// create fallback white texture
	_whiteTexture = textureCreate(pImage);
}

void RS::windowResized(uint32_t width, uint32_t height) {
	_pDevice->windowResize(width, height);

	_width = width;
	_height = height;
}

RenderingServer::RenderingServer(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (strcmp("--validation", argv[i]) == 0)
			_useValidation = true;
	}
}
