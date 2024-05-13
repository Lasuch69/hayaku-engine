#include <cstdint>
#include <filesystem>
#include <optional>

#include "loader.h"

#include "scene.h"

bool Scene::load(const std::filesystem::path &path) {
	std::optional<Loader::Scene> result = Loader::loadGltf(path);

	if (!result.has_value())
		return false;

	Loader::Scene scene = result.value();

	for (const Loader::Material &sceneMaterial : scene.materials) {
		Texture albedo = NULL_HANDLE;
		Texture normal = NULL_HANDLE;
		Texture roughness = NULL_HANDLE;

		std::optional<size_t> albedoIndex = sceneMaterial.albedoIndex;
		if (albedoIndex.has_value()) {
			Image *pAlbedoMap = scene.images[albedoIndex.value()];
			albedo = RS::getInstance().textureCreate(pAlbedoMap);
			_textures.push_back(albedo);

			// free(pAlbedoMap);
		}

		std::optional<size_t> normalIndex = sceneMaterial.normalIndex;
		if (normalIndex.has_value()) {
			Image *pNormalMap = scene.images[normalIndex.value()];
			normal = RS::getInstance().textureCreate(pNormalMap);
			_textures.push_back(normal);

			// free(pNormalMap);
		}

		std::optional<size_t> roughnessIndex = sceneMaterial.roughnessIndex;
		if (roughnessIndex.has_value()) {
			Image *pRoughnessMap = scene.images[roughnessIndex.value()];
			roughness = RS::getInstance().textureCreate(pRoughnessMap);
			_textures.push_back(roughness);

			// free(pRoughnessMap);
		}

		Material material = RS::getInstance().materialCreate(albedo, normal, roughness);
		_materials.push_back(material);
	}

	for (const Loader::Mesh &sceneMesh : scene.meshes) {
		std::vector<RS::Primitive> primitives;
		for (const Loader::Primitive &meshPrimitive : sceneMesh.primitives) {
			uint64_t materialIndex = meshPrimitive.materialIndex;
			Material material = _materials[materialIndex];

			RS::Primitive primitive = {};
			primitive.vertices = meshPrimitive.vertices;
			primitive.indices = meshPrimitive.indices;
			primitive.material = material;

			primitives.push_back(primitive);
		}

		Mesh mesh = RS::getInstance().meshCreate(primitives);
		_meshes.push_back(mesh);
	}

	for (const Loader::MeshInstance &sceneMeshInstance : scene.meshInstances) {
		uint64_t meshIndex = sceneMeshInstance.meshIndex;

		Mesh mesh = _meshes[meshIndex];
		glm::mat4 transform = sceneMeshInstance.transform;

		MeshInstance meshInstance = RS::getInstance().meshInstanceCreate();
		RS::getInstance().meshInstanceSetMesh(meshInstance, mesh);
		RS::getInstance().meshInstanceSetTransform(meshInstance, transform);

		_meshInstances.push_back(meshInstance);
	}

	for (const Loader::Light &sceneLight : scene.lights) {
		if (sceneLight.type == Loader::LightType::Point) {
			glm::vec3 position = glm::vec3(sceneLight.transform[3]);
			float range = sceneLight.range.value_or(0.0f);
			glm::vec3 color = sceneLight.color;
			float intensity = sceneLight.intensity;

			PointLight pointLight = RS::getInstance().pointLightCreate();
			RS::getInstance().pointLightSetPosition(pointLight, position);
			RS::getInstance().pointLightSetRange(pointLight, range);
			RS::getInstance().pointLightSetColor(pointLight, color);
			RS::getInstance().pointLightSetIntensity(pointLight, intensity);

			_pointLights.push_back(pointLight);

			continue;
		}

		if (sceneLight.type == Loader::LightType::Directional) {
			glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
			glm::mat3 rotation = glm::mat3(sceneLight.transform);

			glm::vec3 direction = rotation * front;
			glm::vec3 color = sceneLight.color;
			float intensity = sceneLight.intensity;

			DirectionalLight directionalLight = RS::getInstance().directionalLightCreate();
			RS::getInstance().directionalLightSetDirection(directionalLight, direction);
			RS::getInstance().directionalLightSetIntensity(directionalLight, intensity);
			RS::getInstance().directionalLightSetColor(directionalLight, color);

			_directionalLights.push_back(directionalLight);

			continue;
		}
	}

	for (Image *pImage : scene.images) {
		free(pImage);
	}

	return true;
}

void Scene::clear() {
	for (MeshInstance meshInstance : _meshInstances)
		RS::getInstance().meshInstanceFree(meshInstance);

	for (DirectionalLight directionalLight : _directionalLights)
		RS::getInstance().directionalLightFree(directionalLight);

	for (PointLight pointLight : _pointLights)
		RS::getInstance().pointLightFree(pointLight);

	for (Mesh mesh : _meshes)
		RS::getInstance().meshFree(mesh);

	for (Material material : _materials)
		RS::getInstance().materialFree(material);

	for (Texture texture : _textures)
		RS::getInstance().textureFree(texture);

	_meshInstances.clear();
	_pointLights.clear();
	_meshes.clear();
	_materials.clear();
	_textures.clear();
}
