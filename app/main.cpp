// Mechapunk entry point: opens an OpenGL 4.6 window through NEE and shows a
// title line. The simulation and city rendering will grow from here.
#include <glad/gl.h>
// glad goes first
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>

#include "Camera.hpp"
#include "Shader.hpp"
#include "Text2D.hpp"
#include "TextRenderer.hpp"
#include "Util.hpp"

namespace {
constexpr int kWindowWidth = 1366;
constexpr int kWindowHeight = 768;

int RunGame(GLFWwindow& window) {
    auto textShader = std::make_shared<Shader>();
    textShader->AddShader(ReadFile("shaders/text2d.vsh"), ShaderType::Vertex);
    textShader->AddShader(ReadFile("shaders/text2d.fsh"),
                          ShaderType::Fragment);
    textShader->Link();

    auto font = std::make_shared<Font2d>();
    if (!font->Load("fonts/MagistralIC_UTF-8.fnt")) {
        return EXIT_FAILURE;
    }
    TextRenderer textRenderer(gsl_lite::not_null{textShader});
    if (!textRenderer.RegisterFont(*font)) {
        return EXIT_FAILURE;
    }
    const Camera camera(kWindowWidth, kWindowHeight, 30.0F, 0.1F, 1000.0F);

    FontLine2d title;
    title.SetFont(font);
    title.SetText("MECHAPUNK");
    title.SetPosition(-0.3F, 0.0F);
    title.SetFontSize(72.0F);
    title.SetColor({1.0F, 0.2F, 0.8F, 1.0F});

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.02F, 0.01F, 0.05F, 1.0F);
    while (glfwWindowShouldClose(&window) == 0) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        textRenderer.Render(title, camera);
        glfwSwapBuffers(&window);
        glfwPollEvents();
        if (glfwGetKey(&window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(&window, GLFW_TRUE);
        }
    }
    return EXIT_SUCCESS;
}
}  // namespace

int main() {
    if (glfwInit() == 0) {
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight,
                                          "Mechapunk", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    if (gladLoadGL(static_cast<GLADloadfunc>(glfwGetProcAddress)) == 0 ||
        !GLAD_GL_VERSION_4_6) {
        spdlog::critical("OpenGL 4.6 core is required");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    int result = RunGame(*window);  // GL objects die before the context.
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
