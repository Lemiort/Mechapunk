// Mechapunk: observe a simulated cyberpunk city. The simulation runs on its
// own thread; this file owns the window, input, overlay and save files.
#include <glad/gl.h>
// glad goes first
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
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
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "CityView.hpp"
#include "GameRenderer.hpp"
#include "ImGuiOverlay.hpp"
#include "Inspector.hpp"
#include "OrbitCamera.hpp"
#include "RenderQueue.hpp"
#include "Scene.hpp"
#include "Screenshot.hpp"
#include "SimulationHost.hpp"
#include "imgui.h"
#include "mechapunk/sim/GameTime.hpp"
#include "mechapunk/sim/Save.hpp"
#include "mechapunk/sim/Weather.hpp"
#include "mechapunk/sim/World.hpp"

namespace {

using namespace mechapunk;

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
// Speeds for keys 1 to 6; at x1 a game minute takes two real seconds.
// Times real time: at x1 a game minute takes a real minute.
constexpr std::array<double, 7> kSpeeds{1.0,    2.0,     10.0,    100.0,
                                        1000.0, 10000.0, 100000.0};
constexpr double kOrbitDegreesPerPixel = 0.25;
constexpr float kPanPerSecond = 40.0F;
constexpr double kMessageSeconds = 3.0;
// Pixels the cursor may move between press and release for a click.
constexpr double kClickTolerance = 6.0;
// Looking into a building: close and steep, so walls hide little.
constexpr float kInteriorDistance = 2.2F;
constexpr float kInteriorPitch = 58.0F;
// Looking at one robot.
constexpr float kRobotViewDistance = 0.8F;
constexpr double kWorldDoubleClickSeconds = 0.4;
// Robots outdoors grow with camera distance to stay visible.
constexpr float kFigureScalePerUnit = 0.12F;
constexpr float kMaxFigureScale = 6.0F;
// UI font shipped with the imgui package, copied next to the game.
constexpr const char* kUiFont = "fonts/Roboto-Medium.ttf";
constexpr float kUiFontSize = 17.0F;

struct Options {
    bool startNew{false};
    std::uint64_t seed{1};
    std::uint32_t robots{10000};
    std::filesystem::path savePath{"mechapunk-save.json"};
    // A robot to select and follow from the start, by id; 0 for none.
    std::uint64_t follow{0};
};

std::optional<Options> ParseOptions(std::span<char*> arguments) {
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view name = arguments[index];
        const bool hasValue = index + 1 < arguments.size();
        const auto parse = [&](auto& value) {
            const std::string_view text = arguments[++index];
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value);
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
        } else if (name == "--follow" && hasValue && parse(options.follow)) {
        } else {
            spdlog::error(
                "Usage: mechapunk [--new] [--seed N] [--robots N] "
                "[--save FILE] [--follow ROBOT_ID]");
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
          camera_(city_.Center(), city_.Extent() * 1.1F),
          rain_(renderer_.MakeRain(CityView::kUnitsPerMeter)),
          lastFrameAt_(glfwGetTime()) {
        city_.AddLights(scene_);
        if (options.follow != 0) {
            selection_.robot = sim::EntityId{options.follow};
            following_ = true;
            jumpTo_ = selection_.robot;
        }
        glfwSetWindowUserPointer(&window_, this);
        glfwSetKeyCallback(&window_, &Game::OnKey);
        glfwSetScrollCallback(&window_, &Game::OnScroll);
        glfwSetFramebufferSizeCallback(&window_, &Game::OnResize);
        glfwGetFramebufferSize(&window_, &width_, &height_);
        renderer_.Resize(width_, height_);
        // After the game's own callbacks: ImGui forwards events to them.
        overlay_ = std::make_unique<ImGuiOverlay>(
            window_, ImGuiOverlaySettings{.iniFile = "mechapunk-ui.ini"});
        SetUpUiStyle();
    }

    ~Game() {
        // Restores the game's callbacks while the GL context is current.
        overlay_.reset();
        glfwSetWindowUserPointer(&window_, nullptr);
    }
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
                if (screenshotRequested_) {
                    // Read the finished frame before it is swapped away.
                    screenshotRequested_ = false;
                    TakeScreenshot();
                }
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

    static void OnKey(GLFWwindow* window, int key, int /*scancode*/, int action,
                      int /*mods*/) {
        if (Game* game = From(window); game != nullptr) {
            game->Key(key, action);
        }
    }

    static void OnScroll(GLFWwindow* window, double /*x*/, double y) {
        if (Game* game = From(window);
            game != nullptr && !game->overlay_->WantsMouse()) {
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
        if (action == GLFW_RELEASE || overlay_->WantsKeyboard()) {
            return;
        }
        if (key == GLFW_KEY_N) {
            host_.RequestStep();  // Repeats while held.
        }
        if (action != GLFW_PRESS) {
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            // Step back out of the selection before quitting.
            if (selection_.robot.IsValid() || selection_.room.IsValid()) {
                selection_.robot = {};
                selection_.room = {};
            } else if (selection_.building.IsValid()) {
                selection_ = {};
            } else {
                glfwSetWindowShouldClose(&window_, GLFW_TRUE);
            }
        } else if (key == GLFW_KEY_E || key == GLFW_KEY_PAGE_UP) {
            ChangeFloor(1);
        } else if (key == GLFW_KEY_Q || key == GLFW_KEY_PAGE_DOWN) {
            ChangeFloor(-1);
        } else if (key == GLFW_KEY_SPACE) {
            host_.SetPaused(!host_.IsPaused());
        } else if (key >= GLFW_KEY_1 &&
                   key < GLFW_KEY_1 + static_cast<int>(kSpeeds.size())) {
            host_.SetSpeed(
                kSpeeds.at(static_cast<std::size_t>(key - GLFW_KEY_1)));
        } else if (key == GLFW_KEY_F) {
            following_ = !following_ && selection_.robot.IsValid();
        } else if (key == GLFW_KEY_F12) {
            screenshotRequested_ = true;
        } else if (key == GLFW_KEY_F5) {
            auto saved = host_.Save(savePath_);
            ShowMessage(saved ? std::format("Saved {}", savePath_.string())
                              : std::format("Save failed: {}", saved.error()));
        }
    }

    void HandleHeldInput(float seconds) {
        const bool uiKeyboard = overlay_->WantsKeyboard();
        const auto held = [&](int key) {
            return !uiKeyboard && glfwGetKey(&window_, key) == GLFW_PRESS;
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
            following_ = false;  // Moving the camera away stops following.
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(&window_, &x, &y);
        const bool buttonDown =
            glfwGetMouseButton(&window_, GLFW_MOUSE_BUTTON_LEFT) ==
                GLFW_PRESS ||
            glfwGetMouseButton(&window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        // A press that starts over a window belongs to the UI until release.
        if (buttonDown && !wasDragging_) {
            pressOnUi_ = overlay_->WantsMouse();
        }
        const bool dragging = buttonDown && !pressOnUi_;
        if (dragging && wasDragging_) {
            camera_.Orbit(
                static_cast<float>(-(x - lastCursorX_) * kOrbitDegreesPerPixel),
                static_cast<float>((y - lastCursorY_) * kOrbitDegreesPerPixel));
        }
        // A left press and release with little movement is a click, not a
        // drag.
        const bool leftDown =
            glfwGetMouseButton(&window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (leftDown && !leftWasDown_) {
            clickTravel_ = 0.0;
        } else if (leftDown) {
            clickTravel_ +=
                std::abs(x - lastCursorX_) + std::abs(y - lastCursorY_);
        } else if (leftWasDown_ && clickTravel_ < kClickTolerance &&
                   !pressOnUi_) {
            Click(x, y);
        }
        leftWasDown_ = leftDown;
        wasDragging_ = buttonDown;
        lastCursorX_ = x;
        lastCursorY_ = y;
    }

    void Click(double cursorX, double cursorY) {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(&window_, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0) {
            return;
        }
        // Unproject the cursor to a ray through the scene.
        const Camera camera = camera_.MakeCamera(width_, height_);
        const glm::mat4 inverse =
            glm::inverse(camera.GetViewProjectionMatrix());
        const auto ndcX =
            static_cast<float>((2.0 * cursorX / windowWidth) - 1.0);
        const auto ndcY =
            static_cast<float>(1.0 - (2.0 * cursorY / windowHeight));
        glm::vec4 nearPoint = inverse * glm::vec4{ndcX, ndcY, -1.0F, 1.0F};
        glm::vec4 farPoint = inverse * glm::vec4{ndcX, ndcY, 1.0F, 1.0F};
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;
        const Ray ray{.origin = glm::vec3(nearPoint),
                      .direction = glm::normalize(glm::vec3(farPoint) -
                                                  glm::vec3(nearPoint))};

        const auto pick = city_.PickAt(ray);
        if (!pick) {
            selection_ = {};
        } else if (pick->robot.IsValid()) {
            // Clicking the selected robot again quickly looks at it.
            if (pick->robot == selection_.robot &&
                glfwGetTime() - worldClickAt_ < kWorldDoubleClickSeconds) {
                jumpTo_ = pick->robot;
            }
            worldClickAt_ = glfwGetTime();
            selection_.robot = pick->robot;
            selection_.room = {};
        } else if (pick->room.IsValid()) {
            selection_.room = pick->room;
            selection_.robot = {};
        } else if (pick->building != selection_.building) {
            OpenBuilding(pick->building);
        } else {
            selection_.room = {};
            selection_.robot = {};
        }
    }

    void OpenBuilding(sim::EntityId building) {
        // Habitats open on their first apartment floor, above the stores.
        const sim::Building* found = city_.City().FindBuilding(building);
        const bool habitat =
            found != nullptr && found->kind == sim::BuildingKind::Habitat;
        selection_ = {.building = building,
                      .floor = habitat && found->floors > 1 ? 1U : 0U};
        camera_.Focus(city_.FloorCenter(building, selection_.floor),
                      kInteriorDistance);
        camera_.SetPitch(kInteriorPitch);
    }

    void ChangeFloor(int delta) {
        if (!selection_.building.IsValid()) {
            return;
        }
        const auto floors = static_cast<int>(city_.Floors(selection_.building));
        const int floor = std::clamp(static_cast<int>(selection_.floor) + delta,
                                     0, floors - 1);
        selection_.floor = static_cast<std::uint32_t>(floor);
        // The selected room may be on another floor now.
        selection_.room = {};
        camera_.FocusTarget(
            city_.FloorCenter(selection_.building, selection_.floor));
    }

    // Applies a selection chosen in the inspector, moving the camera to the
    // building and floor it shows.
    void ApplySelection(const Selection& next) {
        if (!next.building.IsValid()) {
            selection_ = next;
            return;
        }
        if (next.building != selection_.building) {
            OpenBuilding(next.building);
        }
        const std::uint32_t floors = city_.Floors(next.building);
        selection_.floor = std::min(next.floor, floors > 0 ? floors - 1 : 0);
        selection_.room = next.room;
        selection_.robot = next.robot;
        camera_.FocusTarget(
            city_.FloorCenter(selection_.building, selection_.floor));
    }

    // Opens the building and floor the followed robot is in, or closes the
    // open building while it walks the streets.
    void KeepFollowedRobotInView(const WorldFrame& frame) {
        const RobotFrame* robot =
            SimulationHost::FindRobot(frame, selection_.robot);
        if (robot == nullptr) {
            following_ = false;
            return;
        }
        if (robot->activity == sim::RobotActivity::Walking) {
            if (selection_.building.IsValid()) {
                selection_ = {.robot = selection_.robot};
            }
            return;
        }
        const auto room = city_.Rooms().Find(robot->location);
        if (room && (selection_.building != room->building->id ||
                     selection_.floor != room->room->floor)) {
            selection_ = {.building = room->building->id,
                          .floor = room->room->floor,
                          .robot = selection_.robot};
        }
    }

    void RenderFrame() {
        const auto frame = host_.LatestFrame();
        const double renderTick = host_.RenderTick();

        overlay_->BeginFrame();
        // The first click on a robot's name opens its page in the inspector,
        // so the second click of a double-click lands on whatever is there
        // now. Take it as "look at" and keep it from reaching that widget.
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            overlay_->WantsMouse() && selection_.robot.IsValid() &&
            selection_.robot == pickedRobot_ &&
            ImGui::GetTime() - pickedAt_ < io.MouseDoubleClickTime) {
            jumpTo_ = selection_.robot;
            io.MouseClicked[ImGuiMouseButton_Left] = false;
            io.MouseDoubleClicked[ImGuiMouseButton_Left] = false;
        }
        DrawStatusWindow(frame.get(), renderTick);
        // A selected robot that was scrapped is gone.
        if (frame != nullptr && selection_.robot.IsValid() &&
            SimulationHost::FindRobot(*frame, selection_.robot) == nullptr) {
            selection_.robot = {};
            following_ = false;
        }
        if (frame != nullptr) {
            if (const auto next = DrawInspector(selection_, *frame, city_,
                                                host_, following_)) {
                ApplySelection(next->selection);
                if (next->jumpToRobot) {
                    jumpTo_ = next->selection.robot;
                } else if (next->selection.robot.IsValid()) {
                    pickedRobot_ = next->selection.robot;
                    pickedAt_ = ImGui::GetTime();
                }
            }
        }
        following_ = following_ && selection_.robot.IsValid();
        if (following_ && frame != nullptr) {
            KeepFollowedRobotInView(*frame);
        }

        const double now = glfwGetTime();
        const auto frameSeconds = static_cast<float>(now - lastFrameAt_);
        lastFrameAt_ = now;
        queue_.Clear();
        if (frame != nullptr) {
            const float figureScale =
                std::clamp(camera_.Distance() * kFigureScalePerUnit, 1.0F,
                           kMaxFigureScale);
            city_.Submit(queue_, *frame, renderTick, selection_, figureScale);
        }
        // Robots are placed by Submit, so the camera moves to them after it.
        if (jumpTo_.IsValid()) {
            if (const auto position = city_.DrawnRobotPosition(jumpTo_)) {
                camera_.Focus(*position, kRobotViewDistance);
            }
            jumpTo_ = {};
        }
        if (following_) {
            if (const auto position =
                    city_.DrawnRobotPosition(selection_.robot)) {
                camera_.FocusTarget(*position);
            }
        }
        rain_->Update(
            frameSeconds, frame != nullptr ? frame->rain : 0, camera_.Target(),
            camera_.MakeCamera(width_, height_).GetPos(), camera_.Distance(),
            CityView::DaylightAt(std::fmod(
                renderTick, static_cast<double>(sim::kMinutesPerDay))));
        rain_->Submit(queue_);
        city_.UpdateLights(
            scene_,
            std::fmod(renderTick, static_cast<double>(sim::kMinutesPerDay)),
            selection_, camera_.Target(), rain_->Level());
        renderer_.Render(scene_, queue_, camera_.MakeCamera(width_, height_));
        overlay_->Render();
    }

    void SetUpUiStyle() {
        ImGuiIO& io = ImGui::GetIO();
        if (std::filesystem::exists(kUiFont)) {
            if (ImFont* font =
                    io.Fonts->AddFontFromFileTTF(kUiFont, kUiFontSize)) {
                io.FontDefault = font;
            }
        } else {
            spdlog::warn("UI font {} not found; using ImGui's default",
                         kUiFont);
        }
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0F;
        style.FrameRounding = 4.0F;
        style.GrabRounding = 4.0F;
        style.Colors[ImGuiCol_WindowBg].w = 0.9F;
        // Neon accents to match the city.
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4{0.45F, 0.1F, 0.4F, 1.0F};
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4{0.1F, 0.75F, 0.85F, 1.0F};
        style.Colors[ImGuiCol_Header] = ImVec4{0.1F, 0.45F, 0.55F, 0.6F};
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4{0.1F, 0.55F, 0.65F, 0.8F};
    }

    void DrawStatusWindow(const WorldFrame* frame, double renderTick) {
        constexpr float kMargin = 10.0F;
        ImGui::SetNextWindowPos(ImVec2{kMargin, kMargin},
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("City", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto tick = static_cast<sim::Tick>(std::floor(renderTick));
            ImGui::TextUnformatted(sim::FormatTime(tick).c_str());
            if (frame != nullptr && frame->rain > 0) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4{0.55F, 0.75F, 1.0F, 1.0F}, "%s",
                    std::string{sim::RainName(frame->rain)}.c_str());
            }

            bool paused = host_.IsPaused();
            if (ImGui::Checkbox("Paused", &paused)) {
                host_.SetPaused(paused);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!paused);
            if (ImGui::Button("Step")) {
                host_.RequestStep();
            }
            ImGui::EndDisabled();

            for (std::size_t index = 0; index < kSpeeds.size(); ++index) {
                if (index > 0) {
                    ImGui::SameLine();
                }
                const std::string label = std::format("x{}", kSpeeds.at(index));
                if (ImGui::RadioButton(label.c_str(),
                                       host_.Speed() == kSpeeds.at(index))) {
                    host_.SetSpeed(kSpeeds.at(index));
                }
            }

            if (frame != nullptr) {
                ImGui::Text("%u resting, %u working, %u out, %u walking",
                            frame->resting, frame->working, frame->visiting,
                            frame->walking);
            }
            ImGui::TextDisabled("%.0f fps", ImGui::GetIO().Framerate);
            if (ImGui::Button("Save")) {
                auto saved = host_.Save(savePath_);
                ShowMessage(
                    saved ? std::format("Saved {}", savePath_.string())
                          : std::format("Save failed: {}", saved.error()));
            }
            ImGui::SameLine();
            if (ImGui::Button("Screenshot")) {
                screenshotRequested_ = true;
            }
            if (glfwGetTime() < messageUntil_) {
                ImGui::TextDisabled("%s", message_.c_str());
            }
            if (ImGui::CollapsingHeader("Controls")) {
                ImGui::BulletText("Click a building, room or robot to inspect");
                ImGui::BulletText("Q / E: floor down / up");
                ImGui::BulletText("Drag: orbit   Wheel: zoom   WASD: move");
                ImGui::BulletText("Space: pause   N: step   1-5: speed");
                ImGui::BulletText(
                    "Double-click a robot or its name: look at it");
                ImGui::BulletText("F: follow the selected robot");
                ImGui::BulletText("F5: save   F12: screenshot");
                ImGui::BulletText("Esc: close the selection, then quit");
            }
            if (ImGui::CollapsingHeader("Display")) {
                // How the frame's light maps to the screen.
                ToneMapping toneMapping = renderer_.GetToneMapping();
                constexpr std::array<const char*, 3> kOperators{
                    "None (clamp)", "Reinhard", "ACES"};
                int selected = static_cast<int>(toneMapping.op);
                bool changed =
                    ImGui::Combo("Tone mapping", &selected, kOperators.data(),
                                 static_cast<int>(kOperators.size()));
                changed |= ImGui::SliderFloat("Exposure", &toneMapping.exposure,
                                              0.25F, 4.0F, "%.2f",
                                              ImGuiSliderFlags_Logarithmic);
                if (changed) {
                    toneMapping.op = static_cast<ToneMapOperator>(selected);
                    renderer_.SetToneMapping(toneMapping);
                }
                BloomSettings bloom = renderer_.GetBloom();
                bool bloomChanged = ImGui::SliderFloat("Bloom", &bloom.strength,
                                                       0.0F, 0.5F, "%.3f");
                bloomChanged |= ImGui::SliderFloat(
                    "Bloom threshold", &bloom.threshold, 0.0F, 4.0F, "%.2f");
                bloomChanged |= ImGui::SliderFloat(
                    "Bloom radius", &bloom.radius, 0.5F, 3.0F, "%.2f");
                if (bloomChanged) {
                    renderer_.SetBloom(bloom);
                }
                // The opaque geometry of the last frame.
                const RenderStatistics& statistics = renderer_.GetStatistics();
                ImGui::Text("Meshes: %zu drawn, %zu culled",
                            statistics.meshesDrawn, statistics.meshesCulled);
                ImGui::Text("Batches: %zu drawn (%zu instances), %zu culled",
                            statistics.batchesDrawn,
                            statistics.batchInstancesDrawn,
                            statistics.batchesCulled);
                ImGui::Text("Draw calls: %zu", statistics.drawCalls);
            }
        }
        ImGui::End();
    }

    void TakeScreenshot() {
        auto path = TimestampedScreenshotPath("screenshots");
        if (!path) {
            ShowMessage(std::format("Screenshot failed: {}", path.error()));
            return;
        }
        auto saved = SaveScreenshot(*path, width_, height_);
        ShowMessage(saved
                        ? std::format("Screenshot saved: {}", path->string())
                        : std::format("Screenshot failed: {}", saved.error()));
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
    std::unique_ptr<RainEffect> rain_;
    double lastFrameAt_{0.0};
    std::unique_ptr<ImGuiOverlay> overlay_;
    bool wasDragging_{false};
    bool pressOnUi_{false};
    bool leftWasDown_{false};
    double clickTravel_{};
    double lastCursorX_{};
    double lastCursorY_{};
    std::string message_;
    double messageUntil_{};
    // When a robot was last picked in the inspector, and when the world was
    // last clicked, to tell double-clicks.
    sim::EntityId pickedRobot_;
    double pickedAt_{-1.0};
    double worldClickAt_{-1.0};
    // The camera stays on the selected robot.
    bool following_{false};
    // Robot to move the camera to once it has been drawn.
    sim::EntityId jumpTo_;
    bool screenshotRequested_{false};
    Selection selection_;
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
