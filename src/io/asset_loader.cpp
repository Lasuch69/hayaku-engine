#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

#include "image_loader.h"
#include "mesh.h"

#include "asset_loader.h"

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

void _generateTangents(const IndexArray &indices, VertexArray &vertices) {
	assert(indices.count % 3 == 0);

	uint32_t averageCount = vertices.count;
	float *pAverage = (float *)calloc(averageCount, sizeof(float));

	for (size_t i = 0; i < indices.count; i += 3) {
		Vertex &v0 = vertices.pData[indices.pData[i + 0]];
		Vertex &v1 = vertices.pData[indices.pData[i + 1]];
		Vertex &v2 = vertices.pData[indices.pData[i + 2]];

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

		pAverage[indices.pData[i + 0]] += 1.0;
		pAverage[indices.pData[i + 1]] += 1.0;
		pAverage[indices.pData[i + 2]] += 1.0;
	}

	for (uint32_t i = 0; i < averageCount; i++) {
		float denom = 1.0 / pAverage[i];
		vertices.pData[i].tangent *= denom;
	}
}

Mesh _loadMesh(const fastgltf::Asset &asset, const fastgltf::Mesh &mesh) {
	uint32_t primitiveCount = mesh.primitives.size();
	Primitive *pPrimitives = (Primitive *)malloc(primitiveCount * sizeof(Primitive));

	size_t idx = 0;
	for (const fastgltf::Primitive &primitive : mesh.primitives) {
		VertexArray vertices = {};
		IndexArray indices = {};

		// due to Options::GenerateMeshIndices, this should always be true
		assert(primitive.indicesAccessor.has_value());
		size_t accessorIndex = primitive.indicesAccessor.value();
		const fastgltf::Accessor &indexAccessor = asset.accessors[accessorIndex];

		indices.pData = (uint32_t *)malloc(indexAccessor.count * sizeof(uint32_t));
		indices.count = indexAccessor.count;

		fastgltf::iterateAccessorWithIndex<uint32_t>(asset, indexAccessor,
				[&](uint32_t index, size_t idx) { indices.pData[idx] = index; });

		{
			size_t accessorIndex = primitive.findAttribute("POSITION")->second;
			const fastgltf::Accessor &positionAccessor = asset.accessors[accessorIndex];

			// required
			if (!positionAccessor.bufferViewIndex.has_value())
				continue;

			vertices.pData = (Vertex *)malloc(positionAccessor.count * sizeof(Vertex));
			vertices.count = positionAccessor.count;

			fastgltf::iterateAccessorWithIndex<glm::vec3>(
					asset, positionAccessor, [&](const glm::vec3 &position, size_t idx) {
						vertices.pData[idx].position = position;

						// initialize tangent
						vertices.pData[idx].tangent = glm::vec3(0.0);
					});
		}

		for (const auto &attribute : primitive.attributes) {
			const char *pName = attribute.first.data();
			const fastgltf::Accessor &accessor = asset.accessors[attribute.second];

			if (!accessor.bufferViewIndex.has_value())
				continue;

			if (strcmp(pName, "NORMAL") == 0) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(
						asset, accessor, [&](const glm::vec3 &normal, size_t idx) {
							vertices.pData[idx].normal = normal;
						});
			}

			if (strcmp(pName, "TEXCOORD_0") == 0) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(
						asset, accessor, [&](const glm::vec2 &texCoord, size_t idx) {
							vertices.pData[idx].uv = texCoord;
						});
			}
		}

		_generateTangents(indices, vertices);

		uint64_t materialIndex = primitive.materialIndex.value_or(0);

		pPrimitives[idx] = {
			vertices,
			indices,
			materialIndex,
		};

		idx++;
	}

	const char *pName = mesh.name.c_str();

	return {
		pPrimitives,
		primitiveCount,
		pName,
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
			std::shared_ptr<Image> albedoMap(_loadImage(asset, image, file.parent_path()));

			if (albedoMap != nullptr) {
				albedoMap->convert(Image::Format::RGBA8);

				uint32_t idx = scene.images.size();
				scene.images.push_back(albedoMap);
				_material.albedoIndex = idx;
			}
		}

		const std::optional<fastgltf::NormalTextureInfo> &normalInfo = material.normalTexture;

		if (normalInfo.has_value()) {
			const fastgltf::Image &image = asset.images[normalInfo->textureIndex];
			std::shared_ptr<Image> normalMap(_loadImage(asset, image, file.parent_path()));

			if (normalMap != nullptr) {
				normalMap->convert(Image::Format::RG8);

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
							_metallicRoughnessimage->getComponent(Image::Channel::B));

					uint32_t idx = scene.images.size();
					scene.images.push_back(metallicMap);

					_material.metallicIndex = idx;
				}

				{
					// roughness in green channel
					std::shared_ptr<Image> roughnessMap(
							_metallicRoughnessimage->getComponent(Image::Channel::G));

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
