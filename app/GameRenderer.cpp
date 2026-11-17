#include "GameRenderer.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "Shader.hpp"
#include "Texture.hpp"
#include "Util.hpp"

namespace mechapunk {

namespace {
constexpr float kExposure = 1.0F;
constexpr float kBloomStrength = 0.4F;

struct ShaderStage {
    std::filesystem::path path;
    ShaderType type;
};

std::shared_ptr<Shader> LoadShader(std::initializer_list<ShaderStage> stages) {
    auto shader = std::make_shared<Shader>();
    for (const auto& [path, type] : stages) {
        shader->AddShader(ReadFile(path), type);
    }
    shader->Link();
    return shader;
}

std::shared_ptr<Texture2D> LoadTexture(const std::string& path) {
    auto texture = std::make_shared<Texture2D>();
    if (!texture->Load(path)) {
        throw std::runtime_error("Unable to load texture " + path);
    }
    return texture;
}

// Flat, mostly matte surface of one color texture.
std::shared_ptr<Material> MakeSurface(
    const std::shared_ptr<Shader>& shader, const std::string& colorTexture,
    const std::shared_ptr<Texture2D>& flatNormal,
    const std::shared_ptr<Texture2D>& specular) {
    auto material = std::make_shared<Material>(gsl_lite::not_null{shader});
    material->SetColorTexture(LoadTexture(colorTexture));
    material->SetNormalTexture(flatNormal);
    material->SetSpecularTexture(specular);
    return material;
}
// A 1x1 texture of one color.
std::shared_ptr<Texture2D> SolidTexture(glm::vec3 color) {
    const std::array<std::uint8_t, 4> pixel{
        static_cast<std::uint8_t>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F),
        static_cast<std::uint8_t>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F),
        static_cast<std::uint8_t>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F),
        255};
    GLuint texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(texture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        pixel.data());
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return std::make_shared<Texture2D>(texture);
}
}  // namespace

MaterialPalette::MaterialPalette(std::shared_ptr<Shader> shader,
                                 std::shared_ptr<Texture2D> flatNormal,
                                 std::shared_ptr<Texture2D> specular)
    : shader_(std::move(shader)),
      flatNormal_(std::move(flatNormal)),
      specular_(std::move(specular)) {}

namespace {
// Colors are shared at 8 bits per channel.
std::uint32_t ColorKey(glm::vec3 color) {
    const auto channel = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
    };
    return (channel(color.r) << 16U) | (channel(color.g) << 8U) | channel(color.b);
}
}  // namespace

std::shared_ptr<Material> MaterialPalette::Make(glm::vec3 color) const {
    auto material = std::make_shared<Material>(gsl_lite::not_null{shader_});
    material->SetColorTexture(SolidTexture(color));
    material->SetNormalTexture(flatNormal_);
    material->SetSpecularTexture(specular_);
    return material;
}

std::shared_ptr<const Material> MaterialPalette::Get(glm::vec3 color) {
    const std::uint32_t key = ColorKey(color);
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        found = cache_.emplace(key, Make(color)).first;
    }
    return found->second;
}

std::shared_ptr<const Material> MaterialPalette::GetGlowing(glm::vec3 color,
                                                            glm::vec3 emissive) {
    const auto key = std::make_pair(ColorKey(color), ColorKey(emissive));
    auto found = glowing_.find(key);
    if (found == glowing_.end()) {
        Glowing glowing{.material = Make(color), .emissive = emissive};
        glowing.material->SetEmissiveColor(emissive * glow_);
        found = glowing_.emplace(key, std::move(glowing)).first;
    }
    return found->second.material;
}

void MaterialPalette::SetGlow(float glow) {
    if (glow == glow_) {
        return;
    }
    glow_ = glow;
    for (auto& [key, glowing] : glowing_) {
        glowing.material->SetEmissiveColor(glowing.emissive * glow_);
    }
}

GameRenderer::GameRenderer(int width, int height) {
    meshShader_ = LoadShader({{"shaders/vertexShader.vsh", ShaderType::Vertex},
                              {"shaders/fragmentShader.fsh",
                               ShaderType::Fragment}});
    geometryShader_ =
        LoadShader({{"shaders/DSGeometryPass.vsh", ShaderType::Vertex},
                    {"shaders/DSGeometryPass.fsh", ShaderType::Fragment}});
    stencilShader_ =
        LoadShader({{"shaders/DSStencilPass.vsh", ShaderType::Vertex}});
    pointLightShader_ =
        LoadShader({{"shaders/DSPointLight.vsh", ShaderType::Vertex},
                    {"shaders/DSPointLight.fsh", ShaderType::Fragment}});
    spotLightShader_ =
        LoadShader({{"shaders/DSSpotLight.vsh", ShaderType::Vertex},
                    {"shaders/DSSpotLight.fsh", ShaderType::Fragment}});
    directionalLightShader_ =
        LoadShader({{"shaders/DSDirectionalLight.vsh", ShaderType::Vertex},
                    {"shaders/DSDirectionalLight.fsh", ShaderType::Fragment}});

    toneMapShader_ =
        LoadShader({{"shaders/tonemap.vsh", ShaderType::Vertex},
                    {"shaders/tonemap.fsh", ShaderType::Fragment}});

    bloomDownsampleShader_ =
        LoadShader({{"shaders/fullscreen.vsh", ShaderType::Vertex},
                    {"shaders/bloom_downsample.fsh", ShaderType::Fragment}});
    bloomUpsampleShader_ =
        LoadShader({{"shaders/fullscreen.vsh", ShaderType::Vertex},
                    {"shaders/bloom_upsample.fsh", ShaderType::Fragment}});

    particleUpdateShader_ =
        LoadShader({{"shaders/particle_update.vsh", ShaderType::Vertex}});
    particleShader_ =
        LoadShader({{"shaders/particle.vsh", ShaderType::Vertex},
                    {"shaders/particle.gsh", ShaderType::Geometry},
                    {"shaders/particle.fsh", ShaderType::Fragment}});

    gBuffer_ = std::make_shared<GBuffer>(width, height);
    forwardPipeline_ =
        std::make_unique<ForwardPipeline>(gsl_lite::not_null{meshShader_});
    const auto passMaterial = [](const std::shared_ptr<Shader>& shader) {
        return gsl_lite::not_null{std::make_shared<Material>(
            gsl_lite::not_null{shader})};
    };
    deferredPipeline_ =
        std::make_unique<DeferredPipeline>(DeferredPipelineResources{
            .gBuffer = gsl_lite::not_null{gBuffer_},
            .geometryShader = gsl_lite::not_null{geometryShader_},
            .stencilShader = gsl_lite::not_null{stencilShader_},
            .pointLightShader = gsl_lite::not_null{pointLightShader_},
            .spotLightShader = gsl_lite::not_null{spotLightShader_},
            .directionalLightShader =
                gsl_lite::not_null{directionalLightShader_},
            .toneMapShader = gsl_lite::not_null{toneMapShader_},
            .bloomDownsampleShader = gsl_lite::not_null{bloomDownsampleShader_},
            .bloomUpsampleShader = gsl_lite::not_null{bloomUpsampleShader_},
            .stencilMaterial = passMaterial(stencilShader_),
            .pointLightMaterial = passMaterial(pointLightShader_),
            .spotLightMaterial = passMaterial(spotLightShader_),
            .directionalLightMaterial =
                passMaterial(directionalLightShader_)});
    // Filmic: bright neon and lamps roll off instead of clipping flat.
    deferredPipeline_->SetToneMapping(
        {.op = ToneMapOperator::Aces, .exposure = kExposure});
    // Neon and bulbs shine above 1 and spill a soft halo.
    deferredPipeline_->SetBloom({.strength = kBloomStrength});

    const auto flatNormal = LoadTexture("textures/normal_up.jpg");
    const auto matte = LoadTexture("textures/mp_specular_low.png");
    const auto surface = [&](const char* texture) {
        return MakeSurface(meshShader_, texture, flatNormal, matte);
    };
    assets_.ground = surface("textures/mp_ground.png");
    assets_.street = surface("textures/mp_street.png");
    assets_.habitat = surface("textures/mp_habitat.png");
    assets_.workplace = surface("textures/mp_workplace.png");
    assets_.tower = surface("textures/mp_tower.png");
    assets_.robot = surface("textures/mp_robot.png");
    assets_.highlight = surface("textures/mp_highlight.png");
    assets_.palette =
        std::make_unique<MaterialPalette>(meshShader_, flatNormal, matte);
    assets_.cube = std::make_unique<Mesh>(gsl_lite::not_null{assets_.ground},
                                          "models/cube.obj");
    assets_.plane = std::make_unique<Mesh>(gsl_lite::not_null{assets_.ground},
                                           "models/normal_plane.obj");
    assets_.sphere = std::make_unique<Mesh>(
        gsl_lite::not_null{assets_.robot}, "models/normal_geosphere.obj");
}

GameRenderer::~GameRenderer() = default;

std::unique_ptr<RainEffect> GameRenderer::MakeRain(float unitsPerMeter) const {
    return std::make_unique<RainEffect>(particleUpdateShader_, particleShader_,
                                        unitsPerMeter);
}

void GameRenderer::Resize(int width, int height) {
    if (width > 0 && height > 0) {
        gBuffer_->Resize(width, height);
        glViewport(0, 0, width, height);
    }
}

void GameRenderer::Render(const Scene& scene, const RenderQueue& queue,
                          const Camera& camera) {
    const SceneView view = scene.CreateView(camera, queue);
    deferredPipeline_->Render(view, *forwardPipeline_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace mechapunk
