#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <variant>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <SDL3/SDL_log.h>

#include "asset_loader.h"
#include "image_loader.h"

const float CANDELA_TO_LUMEN = 12.5663706144; // PI * 4

using namespace AssetLoader;

glm::mat4 _extractTransform(const fastgltf::Node &node, const glm::mat4 &base = glm::mat4(1.0f)) {
	if (const fastgltf::Node::TransformMatrix *pMatrix =
					std::get_if<fastgltf::Node::TransformMatrix>(&node.transform))
		return glm::mat4x4(glm::make_mat4x4(pMatrix->data()));

	if (const fastgltf::TRS *pTransform = std::get_if<fastgltf::TRS>(&node.transform)) {
		glm::vec3 translation = glm::make_vec3(pTransform->translation.data());
		glm::quat rotation = glm::make_quat(pTransform->rotation.data());
		glm::vec3 scale = glm::make_vec3(pTransform->scale.data());

		return glm::translate(base, translation) * glm::toMat4(rotation) * glm::scale(base, scale);
	}

	return base;
}

std::shared_ptr<Image> _loadImage(const fastgltf::Asset &asset, const fastgltf::Image &image,
		const std::filesystem::path &directory) {
	const fastgltf::sources::URI *pFile = std::get_if<fastgltf::sources::URI>(&image.data);

	if (pFile != nullptr) {
		assert(pFile->fileByteOffset == 0); // We don't support offsets with stbi.

		std::filesystem::path path(directory / pFile->uri.path().data());
		assert(path.is_absolute());

		return ImageLoader::loadFromFile(path.c_str());
	}

	const fastgltf::sources::Vector *pVector = std::get_if<fastgltf::sources::Vector>(&image.data);

	if (pVector != nullptr)
		return ImageLoader::loadFromMemory(pVector->bytes.data(), pVector->bytes.size());

	const fastgltf::sources::BufferView *pView =
			std::get_if<fastgltf::sources::BufferView>(&image.data);

	if (pView != nullptr) {
		const fastgltf::BufferView &bufferView = asset.bufferViews[pView->bufferViewIndex];
		const fastgltf::Buffer &buffer = asset.buffers[bufferView.bufferIndex];

		const fastgltf::sources::Vector *pVector =
				std::get_if<fastgltf::sources::Vector>(&buffer.data);

		if (pVector != nullptr)
			return ImageLoader::loadFromMemory(pVector->bytes.data(), pVector->bytes.size());
	}

	return nullptr;
}

void _generateTangents(const std::vector<uint32_t> &indices, std::vector<Vertex> &vertices) {
	assert(indices.size() % 3 == 0);

	std::vector<float> avg(vertices.size());

	for (size_t i = 0; i < indices.size(); i += 3) {
		Vertex &v0 = vertices[indices[i + 0]];
		Vertex &v1 = vertices[indices[i + 1]];
		Vertex &v2 = vertices[indices[i + 2]];

		glm::vec3 pos0 = v0.position;
		glm::vec3 pos1 = v1.position;
		glm::vec3 pos2 = v2.position;

		glm::vec2 uv0 = v0.uv;
		glm::vec2 uv1 = v1.uv;
		glm::vec2 uv2 = v2.uv;

		glm::vec3 deltaPos1 = pos1 - pos0;
		glm::vec3 deltaPos2 = pos2 - pos0;

		glm::vec2 deltaUV1 = uv1 - uv0;
		glm::vec2 deltaUV2 = uv2 - uv0;

		float r = 1.0 / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
		glm::vec3 tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r;

		v0.tangent += tangent;
		v1.tangent += tangent;
		v2.tangent += tangent;

		avg[indices[i + 0]] += 1.0;
		avg[indices[i + 1]] += 1.0;
		avg[indices[i + 2]] += 1.0;
	}

	for (size_t i = 0; i < avg.size(); i++) {
		float denom = 1.0 / avg[i];
		vertices[i].tangent *= denom;
	}
}

Mesh _loadMesh(fastgltf::Asset &asset, const fastgltf::Mesh &mesh) {
	std::vector<Primitive> primitives = {};

	for (const fastgltf::Primitive &primitive : mesh.primitives) {
		std::vector<uint32_t> indices = {};
		std::vector<Vertex> vertices = {};

		{
			// due to Options::GenerateMeshIndices, this should always be true
			assert(primitive.indicesAccessor.has_value());

			size_t accessorIndex = primitive.indicesAccessor.value();
			auto &_indices = asset.accessors[accessorIndex];

			indices.resize(_indices.count);

			size_t idx = 0;
			for (uint32_t index : fastgltf::iterateAccessor<uint32_t>(asset, _indices)) {
				if (idx >= indices.size())
					break;

				indices[idx] = index;
				idx++;
			}
		}

		{
			auto *pIter = primitive.findAttribute("POSITION");
			fastgltf::Accessor &positions = asset.accessors[pIter->second];

			// required
			if (!positions.bufferViewIndex.has_value())
				continue;

			vertices.resize(positions.count);

			size_t idx = 0;
			for (const auto &position : fastgltf::iterateAccessor<glm::vec3>(asset, positions)) {
				if (idx >= vertices.size())
					break;

				vertices[idx].position = position;
				idx++;
			}
		}

		for (const auto &attribute : primitive.attributes) {
			const char *pName = attribute.first.data();
			const size_t accessorIndex = attribute.second;

			if (strcmp(pName, "NORMAL") == 0) {
				fastgltf::Accessor &normals = asset.accessors[accessorIndex];

				if (!normals.bufferViewIndex.has_value())
					continue;

				size_t idx = 0;
				for (const auto &normal : fastgltf::iterateAccessor<glm::vec3>(asset, normals)) {
					if (idx >= vertices.size())
						break;

					vertices[idx].normal = normal;
					idx++;
				}
			}

			if (strcmp(pName, "TEXCOORD_0") == 0) {
				fastgltf::Accessor &uvs = asset.accessors[accessorIndex];

				if (!uvs.bufferViewIndex.has_value())
					continue;

				size_t idx = 0;
				for (const auto &uv : fastgltf::iterateAccessor<glm::vec2>(asset, uvs)) {
					if (idx >= vertices.size())
						break;

					vertices[idx].uv = uv;
					idx++;
				}
			}
		}

		_generateTangents(indices, vertices);

		size_t materialIndex = primitive.materialIndex.value_or(0);

		primitives.push_back(Primitive{
				vertices,
				indices,
				materialIndex,
		});
	}

	return {
		primitives,
		mesh.name.c_str(),
	};
}

Scene AssetLoader::loadGltf(const std::filesystem::path &file) {
	fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual);

	fastgltf::GltfDataBuffer data;
	data.loadFromFile(file);

	fastgltf::Options options = fastgltf::Options::LoadExternalBuffers |
								fastgltf::Options::LoadGLBBuffers |
								fastgltf::Options::GenerateMeshIndices;

	std::filesystem::path assetRoot = file.parent_path();
	fastgltf::Expected<fastgltf::Asset> result = parser.loadGltf(&data, assetRoot, options);

	if (fastgltf::Error err = result.error(); err != fastgltf::Error::None) {
		const char *pMsg = fastgltf::getErrorMessage(err).data();
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Asset loading failed: %s", pMsg);

		return {};
	}

	fastgltf::Asset &asset = result.get();

	Scene scene;

	for (const fastgltf::Material &material : asset.materials) {
		Material _material = {};

		const std::optional<fastgltf::TextureInfo> &albedoInfo = material.pbrData.baseColorTexture;

		if (albedoInfo.has_value()) {
			const fastgltf::Image &image = asset.images[albedoInfo->textureIndex];
			std::shared_ptr<Image> _albedoImage(_loadImage(asset, image, file.parent_path()));

			if (_albedoImage != nullptr) {
				std::shared_ptr<Image> albedoMap(_albedoImage->getColorMap());

				uint32_t idx = scene.images.size();
				scene.images.push_back(albedoMap);
				_material.albedoIndex = idx;
			}
		}

		const std::optional<fastgltf::NormalTextureInfo> &normalInfo = material.normalTexture;

		if (normalInfo.has_value()) {
			const fastgltf::Image &image = asset.images[normalInfo->textureIndex];
			std::shared_ptr<Image> _normalImage(_loadImage(asset, image, file.parent_path()));

			if (_normalImage != nullptr) {
				std::shared_ptr<Image> normalMap(_normalImage->getNormalMap());

				uint32_t idx = scene.images.size();
				scene.images.push_back(normalMap);
				_material.normalIndex = idx;
			}
		}

		const std::optional<fastgltf::TextureInfo> &metallicRoughnessInfo =
				material.pbrData.metallicRoughnessTexture;

		if (metallicRoughnessInfo.has_value()) {
			const fastgltf::Image &image = asset.images[metallicRoughnessInfo->textureIndex];
			std::shared_ptr<Image> _metallicRoughnessimage(
					_loadImage(asset, image, file.parent_path()));

			if (_metallicRoughnessimage != nullptr) {
				{
					// metallic in blue channel
					std::shared_ptr<Image> metallicMap(
							_metallicRoughnessimage->getMetallicMap(Image::Channel::B));

					uint32_t idx = scene.images.size();
					scene.images.push_back(metallicMap);

					_material.metallicIndex = idx;
				}

				{
					// roughness in green channel
					std::shared_ptr<Image> roughnessMap(
							_metallicRoughnessimage->getRoughnessMap(Image::Channel::G));

					uint32_t idx = scene.images.size();
					scene.images.push_back(roughnessMap);

					_material.roughnessIndex = idx;
				}
			}
		}

		scene.materials.push_back(_material);
	}

	for (const fastgltf::Mesh &mesh : asset.meshes) {
		Mesh _mesh = _loadMesh(asset, mesh);
		scene.meshes.push_back(_mesh);
	}

	for (const fastgltf::Node &node : asset.nodes) {
		glm::mat4 transform = _extractTransform(node);
		std::string name = node.name.c_str();

		const fastgltf::Optional<size_t> &meshIndex = node.meshIndex;
		if (meshIndex.has_value()) {
			MeshInstance meshInstance = {
				transform,
				node.meshIndex.value(),
				name,
			};

			scene.meshInstances.push_back(meshInstance);
		}

		const fastgltf::Optional<size_t> &lightIndex = node.lightIndex;
		if (lightIndex.has_value()) {
			fastgltf::Light light = asset.lights[lightIndex.value()];

			LightType lightType = LightType::Point;
			glm::vec3 color = glm::make_vec3(light.color.data());
			float intensity = light.intensity;

			std::optional<float> range = {};

			if (light.type == fastgltf::LightType::Point) {
				if (light.range.has_value())
					range = light.range.value();

				intensity *= CANDELA_TO_LUMEN / 1000.0f;
				lightType = LightType::Point;
			} else if (light.type == fastgltf::LightType::Directional) {
				lightType = LightType::Directional;
			} else {
				continue;
			}

			Light _light = {
				transform,
				lightType,
				color,
				intensity,
				range,
				name,
			};

			scene.lights.push_back(_light);
		}
	}

	return scene;
}