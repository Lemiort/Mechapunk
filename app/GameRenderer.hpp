#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "Camera.hpp"
#include "DebugRenderer.hpp"
#include "DeferredPipeline.hpp"
#include "ForwardPipeline.hpp"
#include "GBuffer.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "Shader.hpp"
#include "Texture.hpp"
#include "mechapunk/sim/City.hpp"

#include <glm/ext/vector_float3.hpp>

namespace mechapunk {

// Solid-color matte surfaces, made on first use and shared by color.
class MaterialPalette {
public:
    MaterialPalette(std::shared_ptr<Shader> shader,
                    std::shared_ptr<Texture2D> flatNormal,
                    std::shared_ptr<Texture2D> specular);
    [[nodiscard]] std::shared_ptr<const Material> Get(glm::vec3 color);
    // A surface of `color` that also gives off light of `emissive`, such as
    // a neon tube, a screen or a lit window. Every glowing surface is scaled
    // by the glow set with SetGlow.
    [[nodiscard]] std::shared_ptr<const Material> GetGlowing(glm::vec3 color,
                                                             glm::vec3 emissive);
    // How strongly glowing surfaces shine, 1 at night.
    void SetGlow(float glow);

private:
    struct Glowing {
        std::shared_ptr<Material> material;
        glm::vec3 emissive;
    };
    [[nodiscard]] std::shared_ptr<Material> Make(glm::vec3 color) const;

    std::shared_ptr<Shader> shader_;
    std::shared_ptr<Texture2D> flatNormal_;
    std::shared_ptr<Texture2D> specular_;
    std::map<std::uint32_t, std::shared_ptr<Material>> cache_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Glowing> glowing_;
    float glow_{1.0F};
};

// Meshes and surface materials shared by everything the game draws.
struct RenderAssets {
    std::unique_ptr<Mesh> cube;    // 1x1x1, base on y = 0
    std::unique_ptr<Mesh> plane;   // 1x1 on y = 0
    std::unique_ptr<Mesh> sphere;  // radius 1
    std::shared_ptr<Material> ground;
    std::shared_ptr<Material> street;
    std::shared_ptr<Material> habitat;
    std::shared_ptr<Material> workplace;
    std::shared_ptr<Material> tower;
    std::shared_ptr<Material> robot;
    std::shared_ptr<Material> highlight;
    // Colors of walls, furniture and robots.
    std::unique_ptr<MaterialPalette> palette;
};

// Owns NEE's deferred pipeline and the game's render assets. Must be created
// and destroyed while the OpenGL context is current.
class GameRenderer {
public:
    GameRenderer(int width, int height);
    ~GameRenderer();
    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;
    GameRenderer(GameRenderer&&) = delete;
    GameRenderer& operator=(GameRenderer&&) = delete;

    [[nodiscard]] RenderAssets& Assets() noexcept { return assets_; }

    void Resize(int width, int height);
    // Draws the queued meshes lit by the scene's lights into the window's
    // framebuffer, which stays bound for UI drawn on top.
    void Render(const Scene& scene, const RenderQueue& queue,
                const Camera& camera);

private:
    std::shared_ptr<Shader> meshShader_;
    std::shared_ptr<Shader> geometryShader_;
    std::shared_ptr<Shader> stencilShader_;
    std::shared_ptr<Shader> pointLightShader_;
    std::shared_ptr<Shader> spotLightShader_;
    std::shared_ptr<Shader> directionalLightShader_;
    std::shared_ptr<GBuffer> gBuffer_;
    std::unique_ptr<ForwardPipeline> forwardPipeline_;
    std::unique_ptr<DeferredPipeline> deferredPipeline_;
    RenderAssets assets_;
};

}  // namespace mechapunk
