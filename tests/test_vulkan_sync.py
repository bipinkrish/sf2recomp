"""Vulkan image allocation, output visibility, and GPU failure handling."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_settings import sdl_flags

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


class VulkanSyncTests(unittest.TestCase):
    def test_resolution_scaling_preserves_cpu_artwork_dimensions(self):
        textures = (ROOT / "runtime/nvn/vulkan/textures.c").read_text()
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { uint32_t target, samples, flags, width, height; } NvnTexture;
static uint32_t screen_width = 1920;
static uint32_t settings_screen_width(uint32_t height) { assert(height == 1080); return screen_width; }
'''
        harness += function(textures, "static bool tex_scalable(")
        harness += r'''
int main(void) {
    unsetenv("LARECOMP_NO_SCALE");
    NvnTexture t = {.target=1, .samples=1, .flags=8, .width=1920, .height=1080};
    assert(tex_scalable(&t));
    // These CPU-uploaded 16:9 images used to shrink to a quarter of their
    // dimensions in a 960x540 window at half resolution, overrunning uploads.
    const uint32_t artwork[][2] = {{2048,1152}, {512,288}, {484,274}, {192,108}};
    for (unsigned i=0; i<sizeof artwork/sizeof artwork[0]; ++i) {
        t.width=artwork[i][0]; t.height=artwork[i][1]; assert(!tex_scalable(&t));
    }
    const uint32_t widths[] = {1728,2520,3840};
    for (unsigned i=0; i<sizeof widths/sizeof widths[0]; ++i) {
        screen_width=t.width=widths[i]; t.height=1080; assert(tex_scalable(&t));
        t.width=1920; assert(!tex_scalable(&t));
    }
    screen_width=t.width=1920;
    t.samples=4; assert(!tex_scalable(&t));
    t.samples=1; t.target=2; assert(!tex_scalable(&t));
    t.target=1; t.flags=0; assert(!tex_scalable(&t));
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-screen-textures-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-Wall", str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_gpu_timeout_reports_progress_and_never_returns_completion(self):
        execute = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()
        harness = r'''
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
static struct { VkDevice dev; VkSemaphore timeline; } g_vk;
static uint64_t submit_seq = 10, stat_wait_ns, cum_wait;
static pthread_mutex_t seq_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *wait_why;
static jmp_buf failure;
static void log_message(const char *format, ...) { (void)format; }
#define LOG(...) log_message(__VA_ARGS__)
#define FATAL(...) longjmp(failure, 1)
static unsigned waits, queries, resolved, dumps, faults;
static VkResult wait_result = VK_SUCCESS;
static uint64_t rec_submitted(void) { return 10; }
static void rec_wait_submitted(uint64_t seq) { (void)seq; }
static uint64_t SDL_GetTicksNS(void) { static uint64_t t; return ++t; }
void rq_watch(const char *what, uint64_t arg) { (void)what; (void)arg; }
static void wait_account(uint64_t ns) { (void)ns; wait_why = 0; }
static void cq_resolve_done(uint64_t seq) { assert(seq == 8); ++resolved; }
void vk_devlost_dump(void) { ++dumps; }
void vk_device_fault_report(void) { ++faults; }
VkResult vkGetSemaphoreCounterValue(VkDevice dev, VkSemaphore semaphore, uint64_t *value) {
    (void)dev; (void)semaphore; ++queries; *value = 5; return VK_SUCCESS;
}
VkResult vkWaitSemaphores(VkDevice dev, const VkSemaphoreWaitInfo *info, uint64_t timeout) {
    (void)dev; assert(*info->pValues == 8 && timeout == 10000000000ull);
    ++waits; return wait_result;
}
'''
        harness += function(execute, "void vk_wait_seq(")
        harness += r'''
int main(void) {
    vk_wait_seq(0); assert(!waits);
    vk_wait_seq(8); assert(resolved == 1 && !dumps && !faults && !queries);
    wait_result = VK_TIMEOUT;
    if (!setjmp(failure)) {
        vk_wait_seq(8);
        assert(!"timeout must stop before callers recycle GPU resources");
    }
    assert(waits == 2 && resolved == 1 && dumps == 1 && faults == 1 && queries == 1);
    wait_result = VK_ERROR_DEVICE_LOST;
    if (!setjmp(failure)) { vk_wait_seq(8); assert(!"device loss must stop"); }
    assert(waits == 3 && resolved == 1 && dumps == 2 && faults == 2);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-gpu-timeout-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-Wall", str(root / "test.c"),
                            "-pthread", "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_frame_begin_publishes_game_image_writes_before_sampling(self):
        device = (ROOT / "runtime/nvn/vulkan/device.c").read_text()
        execute = (ROOT / "runtime/nvn/vulkan/exec.c").read_text()
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#define LOG(...) ((void)0)
#define VK_CHECK(call) assert((call) == VK_SUCCESS)
#define WAITING(call) do { call; } while (0)
#define WAITING_AT(index, call) WAITING(call)
#define FRAMES 3
static struct {
    VkDevice dev; VkSwapchainKHR swapchain; VkExtent2D sc_extent;
    VkImage sc_images[8]; VkImageView sc_views[8];
} g_vk;
static SDL_Window *window;
static VkSemaphore acquire_sem[FRAMES];
static uint64_t slot_seq[FRAMES];
static int slot;
static struct { uint32_t image; VkSemaphore acquired; bool ok, secondary; uint64_t id; } cur_frame;
static bool stopping, source_visible, rendering;
static int width = 1920, height = 1080, global_dependencies, image_dependencies;
static bool rq_stopping(void) { return stopping; }
bool SDL_GetWindowSizeInPixels(SDL_Window *w, int *x, int *y) {
    (void)w; *x = width; *y = height; return true;
}
static uint64_t rec_presents_queued(void) { return 0; }
static void rec_wait_presents(uint64_t n) { (void)n; }
static bool rec_present_recreate(void) { return false; }
static void recreate_swapchain(void) { assert(!"unexpected swapchain recreation"); }
static VkSemaphore binary_semaphore(void) { return (VkSemaphore)1; }
static void vk_wait_seq(uint64_t seq) { (void)seq; }
static uint64_t vk_pending_seq(void) { return 12; }
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)1; }
VkResult vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR s, uint64_t timeout,
                              VkSemaphore semaphore, VkFence fence, uint32_t *image) {
    (void)d; (void)s; (void)timeout; (void)semaphore; (void)fence; *image = 0; return VK_SUCCESS;
}
static void rec_CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo *info) {
    (void)cb;
    assert(!rendering);
    for (uint32_t i = 0; i < info->memoryBarrierCount; ++i) {
        const VkMemoryBarrier2 *b = &info->pMemoryBarriers[i];
        // A global write -> read dependency must cover color output and the
        // output pass's fragment shader, even when no upload buffer was used.
        if ((b->srcStageMask & (VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)) &&
            (b->srcAccessMask & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)) &&
            (b->dstStageMask & (VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)) &&
            (b->dstAccessMask & (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT))) {
            source_visible = true; ++global_dependencies;
        }
    }
    // A barrier on the distinct swapchain image cannot publish the source.
    image_dependencies += info->imageMemoryBarrierCount;
}
static void vk_gpu_timer_window_start(void) {}
static void rec_CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *info) {
    (void)cb; (void)info; rendering = true;
}
static void rec_CmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkViewport *v) {
    (void)cb; (void)first; (void)n; (void)v;
}
static void rec_CmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkRect2D *s) {
    (void)cb; (void)first; (void)n; (void)s;
}
'''
        harness += function(execute, "void vk_full_barrier(")
        harness += function(device, "static void image_barrier(")
        harness += function(device, "bool vk_frame_begin(")
        harness += r'''
int main(void) {
    const float black[4] = {0};
    g_vk.swapchain = (VkSwapchainKHR)1;
    g_vk.sc_extent = (VkExtent2D){1920, 1080};
    stopping = true;
    assert(!vk_frame_begin(black) && !rendering && !global_dependencies);
    stopping = false; width = 0;
    assert(!vk_frame_begin(black) && !rendering && !global_dependencies);
    width = 1920;
    for (int frame = 0; frame < 5; ++frame) {
        // The preceding game submission rendered the source image. Starting
        // the next command buffer, or transitioning only the window image,
        // does not make those writes visible to a sampled read.
        source_visible = false; rendering = false;
        assert(vk_frame_begin(black) && rendering && cur_frame.ok);
        assert(image_dependencies > 0);
        assert(source_visible && "output sampled an unsynchronized game image");
    }
}
'''
        cflags, _ = sdl_flags()
        with tempfile.TemporaryDirectory(prefix="sf2-present-sync-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-Wall", *cflags, str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux NVIDIA allocation workaround")
    def test_small_nvidia_images_use_dedicated_memory(self):
        # Compile the actual image creation path against a fake allocator: this
        # checks the flags that reach VMA, including the unaffected image types.
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include "TEXTURE_SOURCE"
VkG g_vk;
static VmaAllocationCreateFlags last_flags;
static VkImageCreateInfo last_image;
void rt_fatal_msg(const char *fmt, ...) { (void)fmt; abort(); }
void rt_fatal_trace(void) { abort(); }
void vk_log_memory(const char *why) { (void)why; }
VkCommandBuffer vk_upload_cmd(void) { return (VkCommandBuffer)1; }
void rec_CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo *dependency) { (void)cb; (void)dependency; }
void rec_CmdClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout, const VkClearColorValue *value, uint32_t count, const VkImageSubresourceRange *range) { (void)cb; (void)image; (void)layout; (void)value; (void)count; (void)range; }
void rec_CmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout, const VkClearDepthStencilValue *value, uint32_t count, const VkImageSubresourceRange *range) { (void)cb; (void)image; (void)layout; (void)value; (void)count; (void)range; }
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice device, VkFormat format, VkFormatProperties *properties) {
    (void)device; (void)format;
    properties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
}
VkResult vmaCreateImage(VmaAllocator allocator, const VkImageCreateInfo *image, const VmaAllocationCreateInfo *allocation, VkImage *out, VmaAllocation *memory, VmaAllocationInfo *info) {
    (void)allocator; (void)info;
    assert(allocation->usage == VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    last_flags = allocation->flags; last_image = *image;
    *out = (VkImage)1; *memory = (VmaAllocation)1; return VK_SUCCESS;
}
static void check(uint32_t vendor, uint32_t target, uint32_t width, uint32_t height, uint32_t depth, uint32_t samples, int dedicated) {
    g_vk.props.vendorID = vendor;
    NvnTexture texture = {.target = target, .width = width, .height = height, .depth = depth, .levels = 1, .samples = samples, .format = 0x25};
    TexBackend backend = {0};
    create_image(&texture, &backend);
    assert(!!(last_flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT) == dedicated);
    assert(last_image.format == VK_FORMAT_R8G8B8A8_UNORM);
    assert(last_image.usage & VK_IMAGE_USAGE_SAMPLED_BIT);
    assert(last_image.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    assert(backend.width == width && backend.height == height && backend.levels == 1);
}
int main(void) {
#ifdef __linux__
    const int dedicated = 1;
#else
    const int dedicated = 0;
#endif
    check(0x10de, 1, 8, 8, 1, 1, dedicated);
    check(0x10de, 1, 16, 16, 1, 1, dedicated);
    check(0x10de, 1, 17, 8, 1, 1, 0);
    check(0x10de, 1, 8, 17, 1, 1, 0);
    check(0x1002, 1, 8, 8, 1, 1, 0);
    check(0x8086, 1, 8, 8, 1, 1, 0);
    check(0x10de, 2, 8, 8, 8, 1, 0);
    check(0x10de, 4, 8, 8, 8, 1, 0);
    check(0x10de, 1, 8, 8, 1, 4, 0);
}
'''
        harness = harness.replace("TEXTURE_SOURCE", str(ROOT / "runtime/nvn/vulkan/textures.c"))
        cflags, _ = sdl_flags()
        with tempfile.TemporaryDirectory(prefix="sf2-image-allocation-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(harness)
            for name, platform_flags in (("linux", []), ("other", ["-U__linux__"])):
                subprocess.run(["clang", "-O2", "-ffunction-sections", "-fdata-sections",
                                *platform_flags, *cflags, "-I" + str(ROOT / "runtime"),
                                "-I" + str(ROOT / "third_party/smaa"),
                                "-I" + str(ROOT / "third_party/smaa/Textures"),
                                str(root / "test.c"), "-Wl,--gc-sections", "-lm", "-pthread",
                                "-o", str(root / name)], check=True)
                subprocess.run([str(root / name)], check=True)

if __name__ == "__main__":
    unittest.main()
