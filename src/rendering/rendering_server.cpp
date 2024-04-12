#include <cstdint>
#include <iostream>

#include <glm/glm.hpp>

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
	CHECK_IF_VALID(_meshes, mesh, "Mesh");

	MeshRD data = _meshes.remove(mesh).value();

	for (PrimitiveRD primitive : data.primitives) {
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
	CHECK_IF_VALID(_meshInstances, meshInstance, "MeshInstance");
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
	CHECK_IF_VALID(_pointLights, pointLight, "PointLight");
	_pointLights.remove(pointLight);
	_updateLights();
}

Texture RS::textureCreate(Image *pImage) {
	if (pImage == nullptr)
		return 0;

	if (pImage->getFormat() != Image::Format::RGBA8) {
		std::cout << "Image format RGBA8 is required to create texture!" << std::endl;
		return 0;
	}

	TextureRD texture = _pDevice->textureCreate(pImage);
	return _textures.insert(texture);
}

void RS::textureFree(Texture texture) {
	CHECK_IF_VALID(_textures, texture, "Texture");

	TextureRD data = _textures.remove(texture).value();

	_pDevice->imageDestroy(data.image);
	_pDevice->imageViewDestroy(data.imageView);
	_pDevice->samplerDestroy(data.sampler);
}

Material RS::materialCreate(Texture albedoTexture) {
	if (!_textures.has(albedoTexture)) {
		std::cout << "Invalid texture: " << albedoTexture << std::endl;
		albedoTexture = textureCreate(
				Image::create(1, 1, Image::Format::RGBA8, { 255, 255, 255, 255 }).get());
	}

	TextureRD albedo = _textures[albedoTexture];

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
					.setImageView(albedo.imageView)
					.setSampler(albedo.sampler);

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
	CHECK_IF_VALID(_materials, material, "Material");

	_materials.remove(material);
}

void RenderingServer::draw() {
	float aspect = static_cast<float>(_width) / static_cast<float>(_height);

	glm::mat4 proj = _camera.projectionMatrix(aspect);
	glm::mat4 view = _camera.viewMatrix();

	_pDevice->updateUniformBuffer(proj, view, _pointLights.size());

	vk::CommandBuffer commandBuffer = _pDevice->drawBegin();

	for (const auto &[meshInstance, meshInstanceRS] : _meshInstances.map()) {
		const MeshRD &mesh = _meshes[meshInstanceRS.mesh];

		glm::mat4 modelView = meshInstanceRS.transform * view;

		MeshPushConstants constants{};
		constants.model = meshInstanceRS.transform;
		constants.modelViewNormal = glm::transpose(glm::inverse(modelView));

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

void RS::init(const std::vector<const char *> &extensions, bool validation) {
	_pDevice = new RenderingDevice(extensions, validation);
}

vk::Instance RS::getVkInstance() const {
	return _pDevice->getInstance();
}

void RS::windowInit(vk::SurfaceKHR surface, uint32_t width, uint32_t height) {
	_pDevice->init(surface, width, height);

	_width = width;
	_height = height;
}

void RS::windowResized(uint32_t width, uint32_t height) {
	_pDevice->windowResize(width, height);

	_width = width;
	_height = height;
}
