// Mechapunk: observe a simulated cyberpunk city. The simulation runs on its
// own thread; this file owns the window, input, overlay and save files.
#include <glad/gl.h>
// glad goes first
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "CityView.hpp"
#include "GameRenderer.hpp"
#include "OrbitCamera.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "SimulationHost.hpp"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/World.hpp"

namespace {

using namespace mechapunk;

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
// Simulated minutes per real second for keys 1 to 5.
constexpr std::array<double, 5> kSpeeds{1.0, 10.0, 60.0, 300.0, 1200.0};
constexpr double kOrbitDegreesPerPixel = 0.25;
constexpr float kPanPerSecond = 40.0F;
constexpr double kMessageSeconds = 3.0;

struct Options {
    bool startNew{false};
    std::uint64_t seed{1};
    std::uint32_t robots{300};
    std::filesystem::path savePath{"mechapunk-save.json"};
};

std::optional<Options> ParseOptions(std::span<char*> arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view name = arguments[index];
        const bool hasValue = index + 1 < arguments.size();
        const auto parse = [&](auto& value) {
            const std::string_view text = arguments[++index];
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            return error == std::errc{} && end == text.data() + text.size();
        };
        if (name == "--new") {
            options.startNew = true;
        } else if (name == "--seed" && hasValue && parse(options.seed)) {
            options.startNew = true;
        } else if (name == "--robots" && hasValue && parse(options.robots) &&
                   options.robots > 0) {
            options.startNew = true;
        } else if (name == "--save" && hasValue) {
            options.savePath = arguments[++index];
        } else {
            spdlog::error(
                "Usage: mechapunk [--new] [--seed N] [--robots N] "
                "[--save FILE]");
            return std::nullopt;
        }
    }
    return options;
}

// Continues the saved world when there is one, otherwise starts a new city.
sim::World LoadOrCreateWorld(const Options& options) {
    if (!options.startNew && std::filesystem::exists(options.savePath)) {
        auto state = sim::LoadWorldFromFile(options.savePath);
        std::expected<sim::World, std::string> world =
            state ? sim::World::FromState(std::move(*state))
                  : std::unexpected(state.error());
        if (world) {
            spdlog::info("Continuing {} at {}", options.savePath.string(),
                         sim::FormatTime(world->CurrentTick()));
            return std::move(*world);
        }
        // Keep the unreadable save for inspection instead of overwriting it.
        std::filesystem::path broken = options.savePath;
        broken += ".broken";
        std::error_code renameError;
        std::filesystem::rename(options.savePath, broken, renameError);
        spdlog::error(
            "Cannot continue {}: {}. Moved it to {} and started a new city.",
            options.savePath.string(), world.error(), broken.string());
    }
    spdlog::info("Starting a new city: seed {}, {} robots", options.seed,
                 options.robots);
    return sim::World::Generate(
        {.seed = options.seed, .robots = options.robots});
}

class Game {
public:
    Game(GLFWwindow& window, const Options& options, sim::World world)
        : window_(window),
          savePath_(options.savePath),
          renderer_(kWindowWidth, kWindowHeight),
          host_(std::move(world)),
          city_(host_.City(), host_.Seed(), renderer_.Assets()),
          camera_(city_.Center(), city_.Extent() * 1.1F) {
        city_.AddLights(scene_);
        glfwSetWindowUserPointer(&window_, this);
        glfwSetKeyCallback(&window_, &Game::OnKey);
        glfwSetScrollCallback(&window_, &Game::OnScroll);
        glfwSetFramebufferSizeCallback(&window_, &Game::OnResize);
        glfwGetFramebufferSize(&window_, &width_, &height_);
        renderer_.Resize(width_, height_);
    }

    ~Game() { glfwSetWindowUserPointer(&window_, nullptr); }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    int Run() {
        auto last = std::chrono::steady_clock::now();
        while (glfwWindowShouldClose(&window_) == 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto seconds =
                std::chrono::duration<float>(now - last).count();
            last = now;
            if (const auto failure = host_.Failure()) {
                LogFailure(failure);
                return EXIT_FAILURE;
            }
            HandleHeldInput(seconds);
            if (width_ > 0 && height_ > 0) {
                RenderFrame();
                glfwSwapBuffers(&window_);
            }
            glfwPollEvents();
        }
        host_.Stop();
        if (auto saved = host_.Save(savePath_); !saved) {
            spdlog::error("Saving on exit failed: {}", saved.error());
            return EXIT_FAILURE;
        }
        spdlog::info("Saved {}", savePath_.string());
        return EXIT_SUCCESS;
    }

private:
    static Game* From(GLFWwindow* window) {
        return static_cast<Game*>(glfwGetWindowUserPointer(window));
    }

    static void OnKey(GLFWwindow* window, int key, int /*scancode*/,
                      int action, int /*mods*/) {
        if (Game* game = From(window); game != nullptr) {
            game->Key(key, action);
        }
    }

    static void OnScroll(GLFWwindow* window, double /*x*/, double y) {
        if (Game* game = From(window); game != nullptr) {
            game->camera_.Zoom(static_cast<float>(y));
        }
    }

    static void OnResize(GLFWwindow* window, int width, int height) {
        if (Game* game = From(window); game != nullptr) {
            game->width_ = width;
            game->height_ = height;
            game->renderer_.Resize(width, height);
        }
    }

    void Key(int key, int action) {
        if (action == GLFW_RELEASE) {
            return;
        }
        if (key == GLFW_KEY_N) {
            host_.RequestStep();  // Repeats while held.
        }
        if (action != GLFW_PRESS) {
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(&window_, GLFW_TRUE);
        } else if (key == GLFW_KEY_SPACE) {
            host_.SetPaused(!host_.IsPaused());
        } else if (key >= GLFW_KEY_1 &&
                   key < GLFW_KEY_1 + static_cast<int>(kSpeeds.size())) {
            host_.SetSpeed(
                kSpeeds.at(static_cast<std::size_t>(key - GLFW_KEY_1)));
        } else if (key == GLFW_KEY_F5) {
            auto saved = host_.Save(savePath_);
            ShowMessage(saved
                            ? std::format("Saved {}", savePath_.string())
                            : std::format("Save failed: {}", saved.error()));
        }
    }

    void HandleHeldInput(float seconds) {
        const auto held = [&](int key) {
            return glfwGetKey(&window_, key) == GLFW_PRESS;
        };
        const float step = kPanPerSecond * seconds;
        float right = 0.0F;
        float forward = 0.0F;
        if (held(GLFW_KEY_W) || held(GLFW_KEY_UP)) {
            forward += step;
        }
        if (held(GLFW_KEY_S) || held(GLFW_KEY_DOWN)) {
            forward -= step;
        }
        if (held(GLFW_KEY_D) || held(GLFW_KEY_RIGHT)) {
            right += step;
        }
        if (held(GLFW_KEY_A) || held(GLFW_KEY_LEFT)) {
            right -= step;
        }
        if (right != 0.0F || forward != 0.0F) {
            camera_.Pan(right, forward);
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(&window_, &x, &y);
        const bool dragging =
            glfwGetMouseButton(&window_, GLFW_MOUSE_BUTTON_LEFT) ==
                GLFW_PRESS ||
            glfwGetMouseButton(&window_, GLFW_MOUSE_BUTTON_RIGHT) ==
                GLFW_PRESS;
        if (dragging && wasDragging_) {
            camera_.Orbit(
                static_cast<float>(-(x - lastCursorX_) * kOrbitDegreesPerPixel),
                static_cast<float>((y - lastCursorY_) * kOrbitDegreesPerPixel));
        }
        wasDragging_ = dragging;
        lastCursorX_ = x;
        lastCursorY_ = y;
    }

    void RenderFrame() {
        const auto frame = host_.LatestFrame();
        const double renderTick = host_.RenderTick();
        queue_.Clear();
        if (frame != nullptr) {
            city_.Submit(queue_, *frame, renderTick);
        }
        city_.UpdateLights(
            scene_,
            std::fmod(renderTick, static_cast<double>(sim::kMinutesPerDay)));
        SubmitOverlay(frame.get(), renderTick);
        renderer_.Render(scene_, queue_, camera_.MakeCamera(width_, height_));
    }

    void SubmitOverlay(const WorldFrame* frame, double renderTick) {
        const auto tick = static_cast<sim::Tick>(std::floor(renderTick));
        std::string status =
            std::format("{}   x{}", sim::FormatTime(tick), host_.Speed());
        if (host_.IsPaused()) {
            status += "   PAUSED";
        }
        queue_.SubmitText(renderer_.MakeText(status, -0.98F, 0.91F, 32.0F));
        if (frame != nullptr) {
            queue_.SubmitText(renderer_.MakeText(
                std::format("{} at home   {} working   {} walking",
                            frame->atHome, frame->working, frame->commuting),
                -0.98F, 0.84F, 24.0F));
        }
        if (glfwGetTime() < messageUntil_) {
            queue_.SubmitText(
                renderer_.MakeText(message_, -0.98F, 0.78F, 24.0F));
        }
        queue_.SubmitText(renderer_.MakeText(
            "Drag: orbit   Wheel: zoom   WASD: move   Space: pause   N: step "
            "  1-5: speed   F5: save   Esc: save and quit",
            -0.98F, -0.96F, 20.0F));
    }

    void ShowMessage(std::string message) {
        spdlog::info("{}", message);
        message_ = std::move(message);
        messageUntil_ = glfwGetTime() + kMessageSeconds;
    }

    static void LogFailure(const std::exception_ptr& failure) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            spdlog::critical("Simulation failed: {}", error.what());
        } catch (...) {
            spdlog::critical("Simulation failed with an unknown error");
        }
    }

    GLFWwindow& window_;
    std::filesystem::path savePath_;
    int width_{kWindowWidth};
    int height_{kWindowHeight};
    GameRenderer renderer_;
    Scene scene_;
    RenderQueue queue_;
    SimulationHost host_;
    CityView city_;
    OrbitCamera camera_;
    bool wasDragging_{false};
    double lastCursorX_{};
    double lastCursorY_{};
    std::string message_;
    double messageUntil_{};
};

int RunGame(GLFWwindow& window, const Options& options) {
    Game game(window, options, LoadOrCreateWorld(options));
    return game.Run();
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options =
        ParseOptions(std::span(argv, static_cast<std::size_t>(argc)));
    if (!options) {
        return EXIT_FAILURE;
    }
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
    glfwSwapInterval(1);
    if (gladLoadGL(static_cast<GLADloadfunc>(glfwGetProcAddress)) == 0 ||
        !GLAD_GL_VERSION_4_6) {
        spdlog::critical("OpenGL 4.6 core is required");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);

    int result = EXIT_FAILURE;
    try {
        result = RunGame(*window, *options);
    } catch (const std::exception& error) {
        spdlog::critical("Mechapunk failed: {}", error.what());
    }
    // GL resources are released inside RunGame while the context is alive.
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
