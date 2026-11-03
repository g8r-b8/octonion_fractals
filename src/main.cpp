// Vulkan ray-marched 3D fractals: octonion + quaternion julia, mandelbulb,
// mandelbox, sierpinski tetrahedron, menger sponge. Tab cycles fractals.
//
//   tab / shift-tab : cycle fractal
//   WASD/QE         : move camera (forward/back/strafe + down/up)
//   mouse drag      : look around
//   scroll          : zoom (FOV)
//   [ / ]           : zoom out / in (keyboard)
//   ctrl+scroll     : dolly along forward axis
//   space           : pause / unpause animation
//   r               : randomize per-fractal parameters
//   esc             : quit

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>
#include <libgen.h>
#include <chrono>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

namespace {

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 800;
constexpr int kMaxFramesInFlight = 2;

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> kDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#define VK_CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    std::fprintf(stderr, "Vulkan error %d at %s:%d\n", _r, __FILE__, __LINE__); \
    std::abort(); } } while (0)

std::string exeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    return std::string(dirname(buf));
}

std::vector<char> readFile(const std::string& path) {
    // Try the path as given (relative to cwd), then relative to the executable's
    // own directory. Lets the binary run from anywhere.
    std::vector<std::string> candidates = {path, exeDir() + "/" + path};
    for (const auto& p : candidates) {
        std::ifstream f(p, std::ios::ate | std::ios::binary);
        if (!f) continue;
        size_t sz = static_cast<size_t>(f.tellg());
        std::vector<char> buf(sz);
        f.seekg(0);
        f.read(buf.data(), sz);
        return buf;
    }
    throw std::runtime_error("failed to open " + path + " (cwd or " + exeDir() + ")");
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics && present; }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR caps{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Matches push_constant in fractal.frag exactly. std430-ish: each vec4 is 16B.
struct PushConstants {
    float cam_pos[4];
    float cam_right[4];
    float cam_up[4];
    float cam_fwd[4];
    float c_lo[4];
    float c_hi[4];
    float params[4]; // time, aspect, max_iter, bailout
    float hud[4];    // fractal_index, show_help, paused, unused
};
static_assert(sizeof(PushConstants) == 8 * 16, "push constants size");

// One per ray-march fragment shader. Default `c` + `params` populate the
// push-constant fields described in shaders/common.glsl.
struct FractalDef {
    const char* name;
    const char* shader_spv;   // path relative to shaders/ output dir
    float c_default[8];       // c_lo (0..3) then c_hi (0..3)
    float param_z;            // iter count / power / scale, depending on shader
    float param_w;            // bailout / fold limit
    float palette_phase;      // baked into c_hi.w for cosine-palette offset
    bool  animate_c;          // wiggle c over time?
    float cam_pos[3];
    float cam_yaw;
    float cam_pitch;
    float cam_fov;
};

// Order MUST match shaders/gen_font.py's TITLES list (HUD uses the same index).
const FractalDef kFractals[] = {
    // --- Octonion Julias (z^n + c in O) ---
    {"octonion julia z^2",       "fractal.frag.spv",            {-0.45f, 0.62f, 0.10f, 0.0f, 0.08f, 0.0f, 0.0f, 0.0f}, 14.0f, 16.0f,  0.00f, true,  {0.0f, 0.0f, 3.0f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion mandel z^2",      "octonion_mandel.frag.spv",    {0,0,0,0,  0,0,0, 0.05f},                              14.0f, 16.0f,  0.05f, false, {0.0f, 0.0f, 2.2f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^3",       "octonion_cubic.frag.spv",     {-0.32f, 0.55f, -0.10f, 0.0f, 0.04f, 0.0f, 0.0f, 0.10f},14.0f, 16.0f,  0.10f, true,  {0.0f, 0.0f, 2.4f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^4",       "octonion_quartic.frag.spv",   {-0.28f, 0.42f, -0.05f, 0.0f, 0.03f, 0.0f, 0.0f, 0.14f},12.0f, 16.0f,  0.14f, true,  {0.0f, 0.0f, 2.2f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^5",       "octonion_p5.frag.spv",        {-0.24f, 0.36f, -0.04f, 0.0f, 0.02f, 0.0f, 0.0f, 0.18f},12.0f, 16.0f,  0.18f, true,  {0.0f, 0.0f, 2.0f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^6",       "octonion_p6.frag.spv",        {-0.22f, 0.32f, -0.04f, 0.0f, 0.02f, 0.0f, 0.0f, 0.22f},10.0f, 16.0f,  0.22f, true,  {0.0f, 0.0f, 1.9f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^7",       "octonion_p7.frag.spv",        {-0.20f, 0.28f, -0.03f, 0.0f, 0.02f, 0.0f, 0.0f, 0.26f},10.0f, 16.0f,  0.26f, true,  {0.0f, 0.0f, 1.8f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia z^8",       "octonion_p8.frag.spv",        {-0.18f, 0.25f, -0.03f, 0.0f, 0.02f, 0.0f, 0.0f, 0.30f},10.0f, 16.0f,  0.30f, true,  {0.0f, 0.0f, 1.8f}, -1.5707963f, 0.0f, 0.7f},

    // --- Octonion Mandelbrots ---
    {"octonion mandel z^3",      "octonion_mandel_p3.frag.spv", {0,0,0,0,  0,0,0, 0.34f},                              12.0f, 16.0f,  0.34f, false, {0.0f, 0.0f, 1.8f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion mandel z^4",      "octonion_mandel_p4.frag.spv", {0,0,0,0,  0,0,0, 0.38f},                              12.0f, 16.0f,  0.38f, false, {0.0f, 0.0f, 1.6f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion mandel z^5",      "octonion_mandel_p5.frag.spv", {0,0,0,0,  0,0,0, 0.42f},                              10.0f, 16.0f,  0.42f, false, {0.0f, 0.0f, 1.6f}, -1.5707963f, 0.0f, 0.7f},

    // --- Octonion exotics ---
    {"octonion tricorn",         "octonion_tricorn.frag.spv",   {-0.40f, 0.55f, 0.08f, 0.0f, 0.05f, 0.0f, 0.0f, 0.46f}, 14.0f, 16.0f,  0.46f, true,  {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion tricorn z^3",     "octonion_tricorn_p3.frag.spv",{-0.30f, 0.50f, 0.06f, 0.0f, 0.04f, 0.0f, 0.0f, 0.50f}, 12.0f, 16.0f,  0.50f, true,  {0.0f, 0.0f, 2.3f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion burning ship",    "octonion_burning.frag.spv",   {-0.30f, 0.50f, 0.10f, 0.0f, 0.04f, 0.0f, 0.0f, 0.54f}, 14.0f, 16.0f,  0.54f, true,  {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion sin",             "octonion_sin.frag.spv",       {0.30f, 0.20f, 0.10f, 0.0f, 0.0f, 0.0f, 0.0f, 0.58f},   16.0f, 50.0f,  0.58f, true,  {0.0f, 0.0f, 3.5f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion cos",             "octonion_cos.frag.spv",       {0.20f, 0.15f, 0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.62f},   16.0f, 50.0f,  0.62f, true,  {0.0f, 0.0f, 3.5f}, -1.5707963f, 0.0f, 0.7f},

    // --- Octonion slice/hybrid variants ---
    {"octonion slice b",         "octonion_slice_b.frag.spv",   {-0.45f, 0.62f, 0.10f, 0.0f, 0.08f, 0.0f, 0.0f, 0.66f}, 14.0f, 16.0f,  0.66f, true,  {0.0f, 0.0f, 2.4f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion slice c",         "octonion_slice_c.frag.spv",   {-0.45f, 0.62f, 0.10f, 0.0f, 0.08f, 0.0f, 0.0f, 0.70f}, 14.0f, 16.0f,  0.70f, true,  {0.0f, 0.0f, 2.6f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion hybrid z^2 z^3",  "octonion_hybrid_p23.frag.spv",{-0.36f, 0.50f, 0.06f, 0.0f, 0.05f, 0.0f, 0.0f, 0.74f}, 12.0f, 16.0f,  0.74f, true,  {0.0f, 0.0f, 2.2f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion julia + box",     "octonion_hybrid_juliabox.frag.spv",{-0.36f, 0.50f, 0.05f, 0.0f, 0.04f, 0.0f, 0.0f, 0.78f},12.0f, 16.0f, 0.78f, true,  {0.0f, 0.0f, 3.5f}, -1.5707963f, 0.0f, 0.7f},
    {"octonion dual",            "octonion_dual.frag.spv",      {-0.40f, 0.45f, 0.06f, 0.0f, 0.06f, 0.0f, 0.0f, 0.82f}, 12.0f, 16.0f,  0.82f, true,  {0.0f, 0.0f, 2.6f}, -1.5707963f, 0.0f, 0.7f},

    // --- Quaternion family ---
    {"quaternion julia",         "quaternion.frag.spv",         {-0.125f, -0.256f, 0.847f, 0.0895f, 0,0,0, 0.86f},      14.0f, 16.0f,  0.86f, true,  {0.0f, 0.0f, 2.6f}, -1.5707963f, 0.0f, 0.7f},
    {"quaternion mandel",        "quaternion_mandel.frag.spv",  {0,0,0,0, 0,0,0, 0.90f},                                14.0f, 16.0f,  0.90f, false, {0.0f, 0.0f, 2.4f}, -1.5707963f, 0.0f, 0.7f},
    {"quaternion julia z^3",     "quaternion_cubic.frag.spv",   {-0.18f, 0.35f, 0.45f, 0.05f, 0,0,0, 0.94f},            12.0f, 16.0f,  0.94f, true,  {0.0f, 0.0f, 2.2f}, -1.5707963f, 0.0f, 0.7f},
    {"quaternion julia z^4",     "quaternion_quartic.frag.spv", {-0.10f, 0.30f, 0.40f, 0.05f, 0,0,0, 0.02f},            12.0f, 16.0f,  0.02f, true,  {0.0f, 0.0f, 2.0f}, -1.5707963f, 0.0f, 0.7f},
    {"quaternion tricorn",       "quaternion_tricorn.frag.spv", {-0.30f, 0.45f, 0.20f, 0.05f, 0,0,0, 0.06f},            14.0f, 16.0f,  0.06f, true,  {0.0f, 0.0f, 2.4f}, -1.5707963f, 0.0f, 0.7f},

    // --- Mandelbulb family ---
    {"mandelbulb p8",            "mandelbulb.frag.spv",         {0,0,0,0, 0,0,0, 0.10f},                                8.0f,  256.0f, 0.10f, false, {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},
    {"mandelbulb p2",            "mandelbulb_p2.frag.spv",      {0,0,0,0, 0,0,0, 0.14f},                                2.0f,  256.0f, 0.14f, false, {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},
    {"mandelbulb p3",            "mandelbulb_p3.frag.spv",      {0,0,0,0, 0,0,0, 0.18f},                                3.0f,  256.0f, 0.18f, false, {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},
    {"mandelbulb p16",           "mandelbulb_p16.frag.spv",     {0,0,0,0, 0,0,0, 0.22f},                                16.0f, 1024.0f, 0.22f, false, {0.0f, 0.0f, 2.2f}, -1.5707963f, 0.0f, 0.7f},
    {"juliabulb",                "juliabulb.frag.spv",          {0.20f, 0.10f, 0.30f, 0.0f, 0,0,0, 0.26f},              8.0f,  256.0f, 0.26f, true,  {0.0f, 0.0f, 2.5f}, -1.5707963f, 0.0f, 0.7f},

    // --- Mandelbox family ---
    {"mandelbox scale -2",       "mandelbox.frag.spv",          {0,0,0,0, 0,0,0, 0.30f},                                -2.0f, 1.0f,    0.30f, false, {0.0f, 0.0f, 8.0f}, -1.5707963f, 0.0f, 0.7f},
    {"mandelbox scale -3",       "mandelbox_3.frag.spv",        {0,0,0,0, 0,0,0, 0.34f},                                -3.0f, 1.0f,    0.34f, false, {0.0f, 0.0f, 12.0f},-1.5707963f, 0.0f, 0.7f},

    // --- IFS / KIFS ---
    {"sierpinski tetrahedron",   "sierpinski.frag.spv",         {0,0,0,0, 0,0,0, 0.38f},                                10.0f, 0.0f,    0.38f, true,  {1.5f, 1.5f, 1.5f}, -1.5707963f - 0.6f, -0.45f, 0.7f},
    {"menger sponge",            "menger.frag.spv",             {0,0,0,0, 0,0,0, 0.42f},                                5.0f,  0.0f,    0.42f, false, {2.2f, 1.5f, 2.2f}, -1.5707963f - 0.8f, -0.35f, 0.7f},
    {"kifs dodecahedral",        "kifs_dodec.frag.spv",         {0,0,0,0, 0,0,0, 0.46f},                                10.0f, 0.0f,    0.46f, false, {1.8f, 1.8f, 1.8f}, -1.5707963f - 0.6f, -0.4f, 0.7f},
    {"kifs icosahedral",         "kifs_icos.frag.spv",          {0,0,0,0, 0,0,0, 0.50f},                                10.0f, 0.0f,    0.50f, false, {1.5f, 1.5f, 1.5f}, -1.5707963f - 0.6f, -0.4f, 0.7f},
    {"apollonian gasket",        "apollonian.frag.spv",         {0,0,0,0, 0,0,0, 0.54f},                                10.0f, 1.2f,    0.54f, false, {0.0f, 0.0f, 2.0f}, -1.5707963f, 0.0f, 0.7f},
    {"gyroid tpms",              "gyroid.frag.spv",             {0,0,0,0, 0,0,0, 0.58f},                                1.0f,  0.2f,    0.58f, false, {0.0f, 0.0f, 4.0f}, -1.5707963f, 0.0f, 0.7f},
};
constexpr size_t kNumFractals = sizeof(kFractals) / sizeof(kFractals[0]);

struct Camera {
    float pos[3] = {0.0f, 0.0f, 3.0f};
    float yaw = -1.5707963f;   // looking toward -Z
    float pitch = 0.0f;
    float fov_scale = 0.7f;    // tan(fov/2)

    void basis(float right[3], float up[3], float fwd[3]) const {
        float cy = std::cos(yaw), sy = std::sin(yaw);
        float cp = std::cos(pitch), sp = std::sin(pitch);
        fwd[0] = cp * cy; fwd[1] = sp; fwd[2] = cp * sy;
        // world up = (0,1,0). right = fwd × up_world, normalized.
        float rx = fwd[2], rz = -fwd[0];
        float rl = std::sqrt(rx * rx + rz * rz);
        if (rl < 1e-6f) { rx = 1.0f; rz = 0.0f; rl = 1.0f; }
        right[0] = rx / rl; right[1] = 0.0f; right[2] = rz / rl;
        // up = right × fwd
        up[0] = right[1] * fwd[2] - right[2] * fwd[1];
        up[1] = right[2] * fwd[0] - right[0] * fwd[2];
        up[2] = right[0] * fwd[1] - right[1] * fwd[0];
    }
};

class App {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages_;
    VkFormat swapFormat_{};
    VkExtent2D swapExtent_{};
    std::vector<VkImageView> swapImageViews_;
    std::vector<VkFramebuffer> swapFramebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::vector<VkPipeline> pipelines_;            // sized to kNumFractals
    std::vector<bool>       pipelinesBuilt_;       // lazy build: which slots are valid

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlight_;

    size_t currentFrame_ = 0;
    bool framebufferResized_ = false;

    // ---- App state ----
    Camera camera_;
    double lastMouseX_ = 0, lastMouseY_ = 0;
    bool mouseCaptured_ = false;
    bool paused_ = false;
    double startTime_ = 0;
    double pausedTime_ = 0;
    size_t fractalIndex_ = 0;
    bool showHelp_ = true;     // on-screen controls overlay
    bool animEnabled_ = true;  // independent of pause: full toggle of c-animation
    // Per-fractal runtime params (mutable copies of the FractalDef defaults).
    float cBase_[8] = {0};
    float paramZ_ = 0.0f;
    float paramW_ = 0.0f;
    float paletteOffset_ = 0.0f;  // added on top of f.palette_phase
    std::mt19937 rng_{std::random_device{}()};
    bool screenshotRequested_ = false;  // F12 sets this; serviced after present.

    // ---- Init ----
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(kWidth, kHeight, "Octonion Julia (Vulkan)", nullptr, nullptr);
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int) {
            static_cast<App*>(glfwGetWindowUserPointer(w))->framebufferResized_ = true;
        });
        glfwSetKeyCallback(window_, &App::keyCb);
        glfwSetMouseButtonCallback(window_, &App::mouseBtnCb);
        glfwSetCursorPosCallback(window_, &App::cursorCb);
        glfwSetScrollCallback(window_, &App::scrollCb);
        loadFractal(0);
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createPipeline();
        createFramebuffers();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    // ---- Instance / validation ----
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            std::fprintf(stderr, "[vk] %s\n", data->pMessage);
        return VK_FALSE;
    }

    void createInstance() {
        if (kEnableValidation && !checkValidationLayerSupport())
            throw std::runtime_error("validation layers requested but unavailable");

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "octonion_fractals";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        auto exts = getRequiredExtensions();
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();

        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        if (kEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
            ci.ppEnabledLayerNames = kValidationLayers.data();
            fillDebugMessengerCi(dbg);
            ci.pNext = &dbg;
        }
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
    }

    bool checkValidationLayerSupport() {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> avail(count);
        vkEnumerateInstanceLayerProperties(&count, avail.data());
        for (const char* needed : kValidationLayers) {
            bool found = false;
            for (const auto& a : avail) if (std::strcmp(a.layerName, needed) == 0) { found = true; break; }
            if (!found) return false;
        }
        return true;
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        std::vector<const char*> r(glfwExts, glfwExts + glfwCount);
        if (kEnableValidation) r.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return r;
    }

    void fillDebugMessengerCi(VkDebugUtilsMessengerCreateInfoEXT& ci) {
        ci = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = debugCb;
    }

    void setupDebugMessenger() {
        if (!kEnableValidation) return;
        VkDebugUtilsMessengerCreateInfoEXT ci;
        fillDebugMessengerCi(ci);
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(instance_, &ci, nullptr, &debugMessenger_);
    }

    void createSurface() {
        VK_CHECK(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_));
    }

    // ---- Device ----
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) throw std::runtime_error("no Vulkan GPUs");
        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(instance_, &count, devs.data());

        int bestScore = -1;
        for (const auto& d : devs) {
            int s = scoreDevice(d);
            if (s > bestScore) { bestScore = s; physical_ = d; }
        }
        if (physical_ == VK_NULL_HANDLE) throw std::runtime_error("no suitable GPU");

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_, &props);
        std::printf("[vk] using: %s\n", props.deviceName);
    }

    int scoreDevice(VkPhysicalDevice d) {
        auto qf = findQueueFamilies(d);
        if (!qf.complete()) return -1;
        if (!checkDeviceExtensionSupport(d)) return -1;
        auto sc = querySwapchainSupport(d);
        if (sc.formats.empty() || sc.presentModes.empty()) return -1;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        int s = 100;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) s += 1000;
        return s;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice d) {
        QueueFamilyIndices q;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &count, nullptr);
        std::vector<VkQueueFamilyProperties> fams(count);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &count, fams.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.graphics = i;
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (present) q.present = i;
            if (q.complete()) break;
        }
        return q;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice d) {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> avail(count);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &count, avail.data());
        std::set<std::string> req(kDeviceExtensions.begin(), kDeviceExtensions.end());
        for (const auto& e : avail) req.erase(e.extensionName);
        return req.empty();
    }

    SwapchainSupport querySwapchainSupport(VkPhysicalDevice d) {
        SwapchainSupport s;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d, surface_, &s.caps);
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(d, surface_, &n, nullptr);
        s.formats.resize(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(d, surface_, &n, s.formats.data());
        vkGetPhysicalDeviceSurfacePresentModesKHR(d, surface_, &n, nullptr);
        s.presentModes.resize(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(d, surface_, &n, s.presentModes.data());
        return s;
    }

    void createLogicalDevice() {
        auto qf = findQueueFamilies(physical_);
        std::set<uint32_t> unique = {*qf.graphics, *qf.present};
        std::vector<VkDeviceQueueCreateInfo> qcis;
        float prio = 1.0f;
        for (uint32_t i : unique) {
            VkDeviceQueueCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            ci.queueFamilyIndex = i;
            ci.queueCount = 1;
            ci.pQueuePriorities = &prio;
            qcis.push_back(ci);
        }
        VkPhysicalDeviceFeatures feats{};

        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
        ci.pQueueCreateInfos = qcis.data();
        ci.pEnabledFeatures = &feats;
        ci.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
        ci.ppEnabledExtensionNames = kDeviceExtensions.data();
        if (kEnableValidation) {
            ci.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
            ci.ppEnabledLayerNames = kValidationLayers.data();
        }
        VK_CHECK(vkCreateDevice(physical_, &ci, nullptr, &device_));
        vkGetDeviceQueue(device_, *qf.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, *qf.present, 0, &presentQueue_);
    }

    // ---- Swapchain ----
    void createSwapchain() {
        auto sup = querySwapchainSupport(physical_);

        VkSurfaceFormatKHR fmt = sup.formats[0];
        for (const auto& f : sup.formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }

        VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& p : sup.presentModes)
            if (p == VK_PRESENT_MODE_MAILBOX_KHR) { pm = p; break; }

        VkExtent2D ext = sup.caps.currentExtent;
        if (ext.width == std::numeric_limits<uint32_t>::max()) {
            int w, h; glfwGetFramebufferSize(window_, &w, &h);
            ext.width = std::clamp((uint32_t)w, sup.caps.minImageExtent.width, sup.caps.maxImageExtent.width);
            ext.height = std::clamp((uint32_t)h, sup.caps.minImageExtent.height, sup.caps.maxImageExtent.height);
        }

        uint32_t count = sup.caps.minImageCount + 1;
        if (sup.caps.maxImageCount > 0 && count > sup.caps.maxImageCount) count = sup.caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface_;
        ci.minImageCount = count;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        // TRANSFER_SRC is used by the F12 screenshot path (vkCmdCopyImageToBuffer).
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        auto qf = findQueueFamilies(physical_);
        uint32_t qfi[2] = {*qf.graphics, *qf.present};
        if (qf.graphics != qf.present) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = qfi;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform = sup.caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = pm;
        ci.clipped = VK_TRUE;

        VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
        swapImages_.resize(n);
        vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
        swapFormat_ = fmt.format;
        swapExtent_ = ext;
    }

    void createImageViews() {
        swapImageViews_.resize(swapImages_.size());
        for (size_t i = 0; i < swapImages_.size(); ++i) {
            VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ci.image = swapImages_[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = swapFormat_;
            ci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device_, &ci, nullptr, &swapImageViews_[i]));
        }
    }

    void createRenderPass() {
        VkAttachmentDescription color{};
        color.format = swapFormat_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_));
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m;
        VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &m));
        return m;
    }

    void createPipeline() {
        // One layout + cache, shared by all fractals. Pipelines are built
        // LAZILY (see ensurePipeline) — creating all 39 in a row was hanging
        // RADV in the back half of the batch, and most users only visit a
        // few fractals anyway.
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_));

        VkPipelineCacheCreateInfo cci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        VK_CHECK(vkCreatePipelineCache(device_, &cci, nullptr, &pipelineCache_));

        pipelines_.assign(kNumFractals, VK_NULL_HANDLE);
        pipelinesBuilt_.assign(kNumFractals, false);
        ensurePipeline(0);  // pre-build the first one so we never bind null
    }

    void ensurePipeline(size_t i) {
        // initWindow() calls loadFractal(0) before initVulkan(); skip until
        // the device + cache + slots actually exist.
        if (device_ == VK_NULL_HANDLE) return;
        if (i >= pipelines_.size() || pipelinesBuilt_[i]) return;
        std::string spv = std::string("shaders/") + kFractals[i].shader_spv;
        auto vertCode = readFile("shaders/fractal.vert.spv");
        auto fragCode = readFile(spv);
        VkShaderModule vert = createShaderModule(vertCode);
        VkShaderModule frag = createShaderModule(fragCode);
        pipelines_[i] = buildPipeline(vert, frag);
        pipelinesBuilt_[i] = true;
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        std::printf("[pipe %zu/%zu] built %s\n", i + 1, kNumFractals, kFractals[i].name);
    }

    VkPipeline buildPipeline(VkShaderModule vert, VkShaderModule frag) {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vp;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cb;
        pci.pDynamicState = &dyn;
        pci.layout = pipelineLayout_;
        pci.renderPass = renderPass_;
        pci.subpass = 0;
        VkPipeline p;
        VK_CHECK(vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &pci, nullptr, &p));
        return p;
    }

    void createFramebuffers() {
        swapFramebuffers_.resize(swapImageViews_.size());
        for (size_t i = 0; i < swapImageViews_.size(); ++i) {
            VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            ci.renderPass = renderPass_;
            ci.attachmentCount = 1;
            ci.pAttachments = &swapImageViews_[i];
            ci.width = swapExtent_.width;
            ci.height = swapExtent_.height;
            ci.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device_, &ci, nullptr, &swapFramebuffers_[i]));
        }
    }

    void createCommandPool() {
        auto qf = findQueueFamilies(physical_);
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = *qf.graphics;
        VK_CHECK(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_));
    }

    void createCommandBuffers() {
        commandBuffers_.resize(kMaxFramesInFlight);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = commandPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()));
    }

    void createSyncObjects() {
        imageAvailable_.resize(kMaxFramesInFlight);
        renderFinished_.resize(kMaxFramesInFlight);
        inFlight_.resize(kMaxFramesInFlight);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &imageAvailable_[i]));
            VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &renderFinished_[i]));
            VK_CHECK(vkCreateFence(device_, &fi, nullptr, &inFlight_[i]));
        }
    }

    void recreateSwapchain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window_, &w, &h);
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(device_);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createFramebuffers();
    }

    void cleanupSwapchain() {
        for (auto fb : swapFramebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
        for (auto iv : swapImageViews_) vkDestroyImageView(device_, iv, nullptr);
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }

    // ---- Fractal switching ----
    void loadFractal(size_t i) {
        fractalIndex_ = i % kNumFractals;
        ensurePipeline(fractalIndex_);
        const FractalDef& f = kFractals[fractalIndex_];
        std::memcpy(cBase_, f.c_default, sizeof(cBase_));
        paramZ_ = f.param_z;
        paramW_ = f.param_w;
        paletteOffset_ = 0.0f;
        camera_.pos[0] = f.cam_pos[0];
        camera_.pos[1] = f.cam_pos[1];
        camera_.pos[2] = f.cam_pos[2];
        camera_.yaw   = f.cam_yaw;
        camera_.pitch = f.cam_pitch;
        camera_.fov_scale = f.cam_fov;
        std::printf("[fractal %zu] %s   paramZ=%.3g paramW=%.3g\n",
                    fractalIndex_, f.name, paramZ_, paramW_);
    }

    void printState() {
        const FractalDef& f = kFractals[fractalIndex_];
        std::printf("--- state of [%zu] %s ---\n", fractalIndex_, f.name);
        std::printf("  paramZ = %.4ff\n  paramW = %.4ff\n", paramZ_, paramW_);
        std::printf("  palette = %.3f (offset %.3f)\n", f.palette_phase + paletteOffset_, paletteOffset_);
        std::printf("  c       = {");
        for (int i = 0; i < 8; ++i) std::printf("%s%.3ff", i ? ", " : "", cBase_[i]);
        std::printf("}\n");
        std::printf("  camera  = pos(%.2f, %.2f, %.2f) yaw=%.3f pitch=%.3f fov=%.3f\n",
                    camera_.pos[0], camera_.pos[1], camera_.pos[2],
                    camera_.yaw, camera_.pitch, camera_.fov_scale);
    }

    void cycleFractal(int dir) {
        size_t n = kNumFractals;
        loadFractal((fractalIndex_ + (dir > 0 ? 1 : n - 1)) % n);
    }

    // Randomize the fractal-specific parameter(s) — meaning differs per shader.
    void randomizeParams() {
        const FractalDef& f = kFractals[fractalIndex_];
        std::uniform_real_distribution<float> d(-0.8f, 0.8f);
        if (std::string(f.shader_spv).find("quaternion") != std::string::npos) {
            for (int i = 0; i < 4; ++i) cBase_[i] = d(rng_);
        } else if (std::string(f.shader_spv) == "fractal.frag.spv") {
            for (int i = 0; i < 8; ++i) cBase_[i] = d(rng_);
        } else if (std::string(f.shader_spv).find("mandelbulb") != std::string::npos) {
            std::uniform_real_distribution<float> p(3.0f, 10.0f);
            paramZ_ = p(rng_);
        } else if (std::string(f.shader_spv).find("mandelbox") != std::string::npos) {
            std::uniform_real_distribution<float> s(-3.2f, -1.6f);
            paramZ_ = s(rng_);
        } else if (std::string(f.shader_spv).find("sierpinski") != std::string::npos) {
            std::uniform_real_distribution<float> ang(0.0f, 6.28318f);
            paramW_ = ang(rng_);
        } else if (std::string(f.shader_spv).find("menger") != std::string::npos) {
            std::uniform_int_distribution<int> n(3, 6);
            paramZ_ = static_cast<float>(n(rng_));
        }
        std::printf("[randomize]\n");
    }

    // ---- Main loop ----
    void mainLoop() {
        startTime_ = glfwGetTime();
        double lastFrame = startTime_;
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
            double now = glfwGetTime();
            float dt = static_cast<float>(now - lastFrame);
            lastFrame = now;
            handleMovement(dt);
            drawFrame();
        }
        vkDeviceWaitIdle(device_);
    }

    void handleMovement(float dt) {
        float speed = (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 3.0f : 1.0f;
        speed *= dt;
        float right[3], up[3], fwd[3];
        camera_.basis(right, up, fwd);
        auto move = [&](const float v[3], float s) {
            camera_.pos[0] += v[0] * s;
            camera_.pos[1] += v[1] * s;
            camera_.pos[2] += v[2] * s;
        };
        if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) move(fwd, speed);
        if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) move(fwd, -speed);
        if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) move(right, -speed);
        if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) move(right,  speed);
        if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS) camera_.pos[1] += speed;
        if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS) camera_.pos[1] -= speed;

        // Keyboard zoom on '['/']'  (also support '-'/'=' for keyboards without bracket muscle-memory)
        float fovStep = 1.0f + dt * 0.9f;
        if (glfwGetKey(window_, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_MINUS)         == GLFW_PRESS) zoom(fovStep);
        if (glfwGetKey(window_, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS ||
            glfwGetKey(window_, GLFW_KEY_EQUAL)         == GLFW_PRESS) zoom(1.0f / fovStep);

        // Continuous parameter tuning (held keys).
        // ParamZ is the dominant per-fractal knob (iter count, power, scale...).
        // ParamW is the secondary (bailout, fold limit, rotation, thickness...).
        float zStep = dt * std::max(0.5f, std::abs(paramZ_) * 0.5f);
        float wStep = dt * std::max(0.1f, std::abs(paramW_) * 0.5f);
        if (glfwGetKey(window_, GLFW_KEY_COMMA)     == GLFW_PRESS) paramZ_ -= zStep;
        if (glfwGetKey(window_, GLFW_KEY_PERIOD)    == GLFW_PRESS) paramZ_ += zStep;
        if (glfwGetKey(window_, GLFW_KEY_J)         == GLFW_PRESS) paramW_ -= wStep;
        if (glfwGetKey(window_, GLFW_KEY_K)         == GLFW_PRESS) paramW_ += wStep;
        if (glfwGetKey(window_, GLFW_KEY_O)         == GLFW_PRESS) paletteOffset_ -= dt * 0.4f;
        if (glfwGetKey(window_, GLFW_KEY_P)         == GLFW_PRESS) paletteOffset_ += dt * 0.4f;
    }

    void zoom(float factor) {
        camera_.fov_scale = std::clamp(camera_.fov_scale * factor, 0.03f, 2.5f);
    }

    void dolly(float distance) {
        float right[3], up[3], fwd[3];
        camera_.basis(right, up, fwd);
        camera_.pos[0] += fwd[0] * distance;
        camera_.pos[1] += fwd[1] * distance;
        camera_.pos[2] += fwd[2] * distance;
    }

    PushConstants makePushConstants() {
        PushConstants pc{};
        float right[3], up[3], fwd[3];
        camera_.basis(right, up, fwd);
        pc.cam_pos[0] = camera_.pos[0];
        pc.cam_pos[1] = camera_.pos[1];
        pc.cam_pos[2] = camera_.pos[2];
        pc.cam_pos[3] = camera_.fov_scale;
        for (int i = 0; i < 3; ++i) {
            pc.cam_right[i] = right[i];
            pc.cam_up[i]    = up[i];
            pc.cam_fwd[i]   = fwd[i];
        }

        double t = paused_ ? pausedTime_ : (glfwGetTime() - startTime_);
        if (!paused_) pausedTime_ = t;

        const FractalDef& f = kFractals[fractalIndex_];

        float c[8];
        std::memcpy(c, cBase_, sizeof(c));
        bool animLive = f.animate_c && animEnabled_ && !paused_;
        if (animLive) {
            c[1] += 0.15f * std::sin(0.13f * t);
            c[4] += 0.15f * std::cos(0.11f * t);
            c[6] += 0.10f * std::sin(0.07f * t + 1.0f);
        }

        for (int i = 0; i < 4; ++i) pc.c_lo[i] = c[i];
        for (int i = 0; i < 4; ++i) pc.c_hi[i] = c[i + 4];
        pc.c_hi[3] = f.palette_phase + paletteOffset_;

        pc.params[0] = static_cast<float>(t);
        pc.params[1] = static_cast<float>(swapExtent_.width) / static_cast<float>(swapExtent_.height);
        pc.params[2] = paramZ_;
        pc.params[3] = paramW_;

        // Sierpinski: feed slow rotation into the W slot when animating.
        if (animLive && std::string(f.shader_spv).find("sierpinski") != std::string::npos) {
            pc.params[3] = 0.2f * static_cast<float>(t);
        }

        pc.hud[0] = static_cast<float>(fractalIndex_);
        pc.hud[1] = showHelp_ ? 1.0f : 0.0f;
        pc.hud[2] = paused_ ? 1.0f : 0.0f;
        pc.hud[3] = 0.0f;
        return pc;
    }

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, const PushConstants& pc) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = renderPass_;
        rpbi.framebuffer = swapFramebuffers_[imageIndex];
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = swapExtent_;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[fractalIndex_]);

        VkViewport vp{0, 0, (float)swapExtent_.width, (float)swapExtent_.height, 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, swapExtent_};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);

        vkCmdPushConstants(cb, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));
    }

    void drawFrame() {
        vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                           imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) throw std::runtime_error("acquire failed");

        vkResetFences(device_, 1, &inFlight_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

        PushConstants pc = makePushConstants();
        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, pc);

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkSemaphore waitSems[] = {imageAvailable_[currentFrame_]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = waitSems;
        si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &commandBuffers_[currentFrame_];
        VkSemaphore sigSems[] = {renderFinished_[currentFrame_]};
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = sigSems;
        VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &si, inFlight_[currentFrame_]));

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = sigSems;
        VkSwapchainKHR swaps[] = {swapchain_};
        pi.swapchainCount = 1;
        pi.pSwapchains = swaps;
        pi.pImageIndices = &imageIndex;

        r = vkQueuePresentKHR(presentQueue_, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || framebufferResized_) {
            framebufferResized_ = false;
            recreateSwapchain();
        } else if (r != VK_SUCCESS) {
            throw std::runtime_error("present failed");
        }

        currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;

        // Service F12 screenshot request *after* present so the captured frame
        // matches what the user just saw. We sync hard (vkQueueWaitIdle) since
        // this is rare and order-of-operations clarity beats throughput here.
        if (screenshotRequested_) {
            screenshotRequested_ = false;
            saveScreenshot(imageIndex);
        }
    }

    // Find a memory type compatible with `typeBits` and the requested property flags.
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        throw std::runtime_error("no compatible memory type");
    }

    // Copy the just-presented swapchain image to a host-visible buffer and
    // encode as PNG. Filename: octonion_<fractalIndex>_<unix_ms>.png in cwd.
    // Assumes the swapchain was created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    // and format VK_FORMAT_B8G8R8A8_SRGB (we swizzle B<->R for stb).
    void saveScreenshot(uint32_t imageIndex) {
        // Make sure the GPU is fully done with this image before we read it.
        vkQueueWaitIdle(graphicsQueue_);

        const uint32_t w = swapExtent_.width;
        const uint32_t h = swapExtent_.height;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;

        // --- Staging buffer (host-visible, host-coherent) ---
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;

        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device_, &bci, nullptr, &staging));

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device_, staging, &mr);

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(device_, &mai, nullptr, &stagingMem));
        VK_CHECK(vkBindBufferMemory(device_, staging, stagingMem, 0));

        // --- One-shot command buffer to do the layout transitions + copy. ---
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool = commandPool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device_, &cbai, &cb));

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        VkImage src = swapImages_[imageIndex];

        // PRESENT_SRC_KHR -> TRANSFER_SRC_OPTIMAL
        VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toSrc.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = src;
        toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSrc);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &region);

        // TRANSFER_SRC_OPTIMAL -> PRESENT_SRC_KHR (so the image is presentable again)
        VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = src;
        toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toPresent);

        VK_CHECK(vkEndCommandBuffer(cb));

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(graphicsQueue_));

        // --- Map, swizzle BGRA->RGBA, encode PNG. ---
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(device_, stagingMem, 0, bytes, 0, &mapped));
        std::vector<uint8_t> rgba(bytes);
        std::memcpy(rgba.data(), mapped, bytes);
        vkUnmapMemory(device_, stagingMem);

        // Swapchain format is VK_FORMAT_B8G8R8A8_SRGB; stb wants RGBA.
        for (VkDeviceSize i = 0; i < bytes; i += 4) {
            std::swap(rgba[i + 0], rgba[i + 2]);
            rgba[i + 3] = 255;  // force opaque alpha
        }

        // Filename: octonion_<fractalIndex>_<unix_ms>.png in cwd.
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count();
        char filename[256];
        std::snprintf(filename, sizeof(filename), "octonion_%zu_%lld.png",
                      fractalIndex_, static_cast<long long>(ms));

        int ok = stbi_write_png(filename, static_cast<int>(w), static_cast<int>(h),
                                4, rgba.data(), static_cast<int>(w * 4));
        if (ok) {
            std::printf("[screenshot] saved %s\n", filename);
        } else {
            std::fprintf(stderr, "[screenshot] FAILED to write %s\n", filename);
        }

        // --- Tear down. ---
        vkFreeCommandBuffers(device_, commandPool_, 1, &cb);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
    }

    // ---- Input callbacks ----
    static void keyCb(GLFWwindow* w, int key, int, int action, int mods) {
        if (action != GLFW_PRESS) return;
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        switch (key) {
            case GLFW_KEY_ESCAPE:    glfwSetWindowShouldClose(w, GLFW_TRUE); break;
            case GLFW_KEY_SPACE:     self->paused_ = !self->paused_; break;
            case GLFW_KEY_R:         self->randomizeParams(); break;
            case GLFW_KEY_TAB:       self->cycleFractal((mods & GLFW_MOD_SHIFT) ? -1 : +1); break;
            case GLFW_KEY_H:         self->showHelp_ = !self->showHelp_; break;
            case GLFW_KEY_T:         self->animEnabled_ = !self->animEnabled_;
                                     std::printf("[anim %s]\n", self->animEnabled_ ? "on" : "off"); break;
            case GLFW_KEY_BACKSPACE: self->loadFractal(self->fractalIndex_); break;  // reset to defaults
            case GLFW_KEY_Y:         self->printState(); break;
            case GLFW_KEY_F12:       self->screenshotRequested_ = true; break;
            default: break;
        }
    }

    static void mouseBtnCb(GLFWwindow* w, int btn, int action, int) {
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (btn != GLFW_MOUSE_BUTTON_LEFT) return;
        if (action == GLFW_PRESS) {
            self->mouseCaptured_ = true;
            glfwGetCursorPos(w, &self->lastMouseX_, &self->lastMouseY_);
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if (action == GLFW_RELEASE) {
            self->mouseCaptured_ = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    static void scrollCb(GLFWwindow* w, double xoffset, double yoffset) {
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        (void)xoffset;
        bool ctrl = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        if (ctrl) {
            // Dolly: scroll up moves forward.
            self->dolly(static_cast<float>(yoffset) * 0.15f);
        } else {
            // Zoom: each notch up = 10% narrower FOV (zoom in).
            float factor = std::pow(0.9f, static_cast<float>(yoffset));
            self->zoom(factor);
        }
    }

    static void cursorCb(GLFWwindow* w, double x, double y) {
        auto* self = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (!self->mouseCaptured_) return;
        double dx = x - self->lastMouseX_;
        double dy = y - self->lastMouseY_;
        self->lastMouseX_ = x;
        self->lastMouseY_ = y;
        const float sens = 0.0035f;
        self->camera_.yaw   += static_cast<float>(dx) * sens;
        self->camera_.pitch -= static_cast<float>(dy) * sens;
        const float pmax = 1.55334f; // ~89°
        if (self->camera_.pitch >  pmax) self->camera_.pitch =  pmax;
        if (self->camera_.pitch < -pmax) self->camera_.pitch = -pmax;
    }

    // ---- Cleanup ----
    void cleanup() {
        cleanupSwapchain();
        for (auto p : pipelines_) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device_, p, nullptr);
        if (pipelineCache_ != VK_NULL_HANDLE) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
            vkDestroySemaphore(device_, renderFinished_[i], nullptr);
            vkDestroyFence(device_, inFlight_[i], nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
        if (debugMessenger_) {
            auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
            if (fn) fn(instance_, debugMessenger_, nullptr);
        }
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        vkDestroyInstance(instance_, nullptr);
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
};

} // namespace

int main() {
    try {
        App app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
