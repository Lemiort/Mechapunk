#include "GameRenderer.hpp"

#include <glad/gl.h>

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
}  // namespace

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
    textShader_ = LoadShader({{"shaders/text2d.vsh", ShaderType::Vertex},
                              {"shaders/text2d.fsh", ShaderType::Fragment}});

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
            .stencilMaterial = passMaterial(stencilShader_),
            .pointLightMaterial = passMaterial(pointLightShader_),
            .spotLightMaterial = passMaterial(spotLightShader_),
            .directionalLightMaterial =
                passMaterial(directionalLightShader_)});

    font_ = std::make_shared<Font2d>();
    if (!font_->Load("fonts/MagistralIC_UTF-8.fnt")) {
        throw std::runtime_error("Unable to load the interface font");
    }
    textRenderer_ =
        std::make_unique<TextRenderer>(gsl_lite::not_null{textShader_});
    if (!textRenderer_->RegisterFont(*font_)) {
        throw std::runtime_error("Unable to register the interface font");
    }

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
    assets_.cube = std::make_unique<Mesh>(gsl_lite::not_null{assets_.ground},
                                          "models/cube.obj");
    assets_.plane = std::make_unique<Mesh>(gsl_lite::not_null{assets_.ground},
                                           "models/normal_plane.obj");
    assets_.sphere = std::make_unique<Mesh>(
        gsl_lite::not_null{assets_.robot}, "models/normal_geosphere.obj");
}

GameRenderer::~GameRenderer() = default;

void GameRenderer::Resize(int width, int height) {
    if (width > 0 && height > 0) {
        gBuffer_->Resize(width, height);
        glViewport(0, 0, width, height);
    }
}

void GameRenderer::Render(const Scene& scene, RenderQueue& queue,
                          const Camera& camera) {
    const SceneView view = scene.CreateView(camera, queue);
    deferredPipeline_->Render(view, *forwardPipeline_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    textRenderer_->Render(queue.GetText(), camera);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

FontLine2d GameRenderer::MakeText(std::string text, float x, float y,
                                  float size) const {
    FontLine2d line;
    line.SetFont(font_);
    line.SetText(std::move(text));
    line.SetPosition(x, y);
    line.SetFontSize(size);
    return line;
}

}  // namespace mechapunk
