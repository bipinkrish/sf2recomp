#include "../../constants/nvn_vulkan.h"
// Vulkan device, window, swapchain and presentation (counterpart of the window/present half of metal.m).
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../paths.h"
#include "../../game_info.h"
#include "../../hle/movie.h"
#include "../../rt.h"  // (HEAP_MAX: lazy_heap_predicted)
#include "../../settings.h"
#include "../../ui/menu.h"
#include "../rqueue.h"
#include "vk.h"
#include "record.h"
#include "../output_scale.h"
#include "../renderer.h"
#include "../shader_prep.h"
#include <pthread.h>

VkG g_vk;
static SDL_Window *window;

// The system memory heap a whole-heap import (exec.c host_import) lands in and what it can hold: the largest heap with
// a host-visible memory type that isn't device-local (all of them device-local: the largest host-visible one), the
// smaller of its size and its budget (budget 0: unknown). -1: no host-visible heap. Plain types, so
// tools/test_gmem_lowmem.py builds it without Vulkan headers.
typedef struct { uint64_t size, budget; bool device_local, host_visible; } ImportHeap;
static int import_heap_room(const ImportHeap *h, uint32_t n, uint64_t *room) {
    int best = -1;
    for (int pass = 0; pass < SF2C_DEVICE_N_2_D473 && best < 0; pass++)
        for (uint32_t i = 0; i < n; i++)
            if (h[i].host_visible && (pass || !h[i].device_local) && (best < 0 || h[i].size > h[best].size)) best = (int)i;
    if (best >= 0) *room = h[best].budget && h[best].budget < h[best].size ? h[best].budget : h[best].size;
    return best;
}
// What the heap must hold beyond the import: the transient ring's chunks once they spill to system memory (64 MB
// each), readback, the driver's own. Issue #205 (8 GB laptop, Ryzen 5 5500U, AMD's Windows driver): system heap
// 2738 MB, budget 2601 - the 3220 MB import was accepted, then the first submission failed with out of device memory.
// An 8 GB desktop with a GPU of its own (system heap ~3950 MB, budget ~3840) stays on one import.
#define IMPORT_MARGIN (SF2C_DEVICE_N_256ULL_3175 << SF2C_DEVICE_N_20_F5CA)
// Under this much room one import should fit but might not (a driver's own limits, other programs): the import is
// tried once at the start (exec.c whole_import_probe_oom), with buffer device addresses enabled so lazy chunks can take
// over. Machines with more (an RTX with 16 GB or more of RAM) skip the probe and keep the device as before.
#define IMPORT_PROBE_BELOW (SF2C_DEVICE_N_2_D473 * HEAP_MAX)

// Whether one import of the whole heap fits its system memory heap (import_heap_room), from the physical device;
// *probe: it fits, but not by much (IMPORT_PROBE_BELOW), or SF2RECOMP_VK_SIM_HEAP_OOM=1 (the probe then fails).
static bool whole_import_fits(bool budget_ext, bool *probe) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT bp = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 mp2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
                                             .pNext = budget_ext ? &bp : NULL};
    vkGetPhysicalDeviceMemoryProperties2(g_vk.phys, &mp2);
    const VkPhysicalDeviceMemoryProperties *mp = &mp2.memoryProperties;
    ImportHeap h[VK_MAX_MEMORY_HEAPS] = {{0}};
    for (uint32_t i = 0; i < mp->memoryHeapCount; i++) {
        h[i].size = mp->memoryHeaps[i].size;
        h[i].budget = budget_ext ? bp.heapBudget[i] : 0;
        h[i].device_local = (mp->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
    }
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if (mp->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) h[mp->memoryTypes[i].heapIndex].host_visible = true;
    uint64_t room = 0;
    int heap = import_heap_room(h, mp->memoryHeapCount, &room);
    const char *sim = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_SIM_HEAP_OOM_49B8);
    if (heap < 0 || room >= HEAP_MAX + IMPORT_MARGIN) {
        *probe = (heap >= 0 && room < IMPORT_PROBE_BELOW) || (sim && atoi(sim));
        return true;
    }
    LOG(SF2C_DEVICE_S_VK_GUEST_HEAP_MEMORY_HEAP_D__4307
        SF2C_DEVICE_S_IMPORTED_IN_CHUNKS_AS_THE_GA_905F, heap, (unsigned long long)(room >> SF2C_DEVICE_N_20_F5CA),
        (unsigned long long)(h[heap].size >> SF2C_DEVICE_N_20_F5CA), (unsigned long long)(h[heap].budget >> SF2C_DEVICE_N_20_F5CA),
        (unsigned long long)((HEAP_MAX + IMPORT_MARGIN) >> SF2C_DEVICE_N_20_F5CA));
    return false;
}

// Whether the guest heap will be imported chunk by chunk (exec.c gmem_lazy): on a GPU whose storage buffers can't
// span the heap (maxStorageBufferRange < HEAP_MAX: Intel, 1 GB); where the system memory heap or its budget can't hold
// one import of the whole heap (whole_import_fits: 8 GB laptops, issue #205); and on RADV when the dma-buf heap
// (exec.c guest_heap_dmabuf) has no cached host-visible memory type that holds it - the same test, which needs only
// the physical device - or SF2RECOMP_VK_GMEM_LAZY=1 (=0: never; a GPU with a short range then gets windows of one
// import, exec.c gmem_chunk_shift). Decided before the device exists because the lazy import needs buffer device
// addresses, and on RADV enabling them puts every allocation on the driver's global BO list (amdgpu checks all of
// them each submission): a device that imports the heap another way shouldn't pay for that.
// *probe: one import is to be tried first (whole_import_fits), lazy chunks if it runs out of memory.
static bool lazy_heap_predicted(bool budget_ext, bool *probe) {
    *probe = false;
    if (g_vk.managed_heap || g_vk.staged_heap) return false;
    const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_GMEM_LAZY_EABE);
    if (e) return atoi(e) != 0;
    if (getenv(SF2C_DEVICE_S_SF2RECOMP_VK_NO_GMEM_2D48)) return false;
    uint64_t lim = g_vk.props.limits.maxStorageBufferRange;
    if (lim && lim < HEAP_MAX) return true;  // (UHD 770 main pass: lazy 8.4 ms, 512 MB windows 8.5)
#ifdef _WIN32
    // The Windows AMD integrated driver accepts one import of the whole heap (the probe writes both ends) and then
    // faults a shader read of it: device lost, as soon as a save loads. Chunks stay inside the pages it maps.
    if (g_vk.props.vendorID == SF2C_DEVICE_N_0X1002_D106 && g_vk.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        return true;
#endif
    if (!whole_import_fits(budget_ext, probe)) return true;
    if (!strstr(g_vk.props.deviceName, SF2C_DEVICE_S_RADV_6945)) return false;
    *probe = false;  // (RADV: as before - the dma-buf heap or lazy chunks, no probe)
#ifdef __linux__
    const char *d = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_HEAP_DMABUF_419D);
    if ((d ? atoi(d) != 0 : true) && (g_vk.dmabuf || g_vk.map_placed)) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((mp.memoryTypes[i].propertyFlags & want) == want && mp.memoryHeaps[mp.memoryTypes[i].heapIndex].size >= HEAP_MAX) return false;
    }
#endif
    return true;
}

#ifdef __linux__
static uint64_t managed_heap_size(const VkPhysicalDeviceMemoryProperties *mp, bool small) {
    if (!small)
        for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
            if ((mp->memoryTypes[i].propertyFlags & VK_MANAGED_HEAP_FLAGS) == VK_MANAGED_HEAP_FLAGS &&
                mp->memoryHeaps[mp->memoryTypes[i].heapIndex].size >= HEAP_MAX)
                return HEAP_MAX;
    return VK_MANAGED_HEAP_SMALL;
}
#endif
static atomic_bool ui_ready;
static VkDebugUtilsMessengerEXT messenger;
static void exit_after_shot(void);  // (UI thread section)

// ================================================================== instance / device
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT type,
                                               const VkDebugUtilsMessengerCallbackDataEXT *d, void *user) {
    (void)type, (void)user;
    static atomic_uint n;
    bool stop = sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT && getenv(SF2C_DEVICE_S_SF2RECOMP_VK_VALIDATION_STOP_43F5);
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT &&
        (stop || atomic_fetch_add_explicit(&n, 1, memory_order_relaxed) < SF2C_DEVICE_N_500_0604)) LOG(SF2C_DEVICE_S_VK_S_N_F968, d->pMessage);
    if (stop) {
        LOG(SF2C_DEVICE_S_VK_STOPPING_AT_THE_FIRST_VAL_7704);
        fflush(stderr);
        _Exit(EXIT_FAILURE);  // do not run GPU cleanup after the validation failure
    }
    return VK_FALSE;
}

// SF2RECOMP_VK_VALIDATION=1: Khronos validation layer (MSYS2 package vulkan-validation-layers)
// SF2RECOMP_VK_VALIDATION_STOP=1: require that layer and its callback, and exit nonzero on the first error.
static bool validating;  // the validation layer is on (validation_exit)
static void create_instance(void) {
    Uint32 n_sdl = 0;
    const char *const *sdl_ext = SDL_Vulkan_GetInstanceExtensions(&n_sdl);
    const char *ext[SF2C_DEVICE_N_16_B17E];
    uint32_t n_ext = 0;
    for (Uint32 i = 0; i < n_sdl && n_ext < SF2C_DEVICE_N_14_8527; i++) ext[n_ext++] = sdl_ext[i];
    bool strict = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_VALIDATION_STOP_43F5) != NULL;
    bool validate = strict || getenv(SF2C_DEVICE_S_SF2RECOMP_VK_VALIDATION_1BC8) != NULL;
#ifdef __APPLE__
    // The Vulkan loader hides MoltenVK devices unless portability is requested.
    ext[n_ext++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
    if (validate) ext[n_ext++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const char *layers[] = {SF2C_DEVICE_S_VK_LAYER_KHRONOS_VALIDATION_84A0};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = SF2_APP_NAME,
                             .applicationVersion = game_application_version(),
                             .pEngineName = SF2C_DEVICE_S_SF2RECOMP_50A6, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                               .enabledExtensionCount = n_ext, .ppEnabledExtensionNames = ext,
                               .enabledLayerCount = validate ? 1 : 0, .ppEnabledLayerNames = layers};
#ifdef __APPLE__
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VkResult r = vkCreateInstance(&ci, NULL, &g_vk.inst);
    if (r != VK_SUCCESS && validate) {
        if (strict) {
            LOG(SF2C_DEVICE_S_VK_CANNOT_CREATE_AN_INSTANCE_F6FF, r);
            fflush(stderr);
            _Exit(EXIT_FAILURE);
        }
        LOG(SF2C_DEVICE_S_VK_VALIDATION_LAYER_UNAVAILA_799D, r);
        ci.enabledLayerCount = 0;
        ci.enabledExtensionCount--;
        validate = false;
        r = vkCreateInstance(&ci, NULL, &g_vk.inst);
    }
    validating = validate;
    if (r != VK_SUCCESS) FATAL(SF2C_DEVICE_S_VKCREATEINSTANCE_D_IS_A_VULK_5AA9, r);
    if (validate) {
        PFN_vkCreateDebugUtilsMessengerEXT f =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_vk.inst, SF2C_DEVICE_S_VKCREATEDEBUGUTILSMESSENGERE_9C7A);
        VkDebugUtilsMessengerCreateInfoEXT mi = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_cb};
        VkResult result = f ? f(g_vk.inst, &mi, NULL, &messenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
        if (result != VK_SUCCESS && strict) {
            LOG(SF2C_DEVICE_S_VK_REQUIRED_VALIDATION_CALLB_C86C, result);
            fflush(stderr);
            _Exit(EXIT_FAILURE);
        }
        if (result == VK_SUCCESS) LOG(SF2C_DEVICE_S_VK_VALIDATION_ENABLED_S_N_E9F8, strict ? SF2C_DEVICE_S_STOP_ON_ERROR_A8C9 : SF2C_DEVICE_S_EMPTY_12AE);
    }
}

static bool has_ext(VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return true;
    return false;
}

// Discrete GPU first (SF2RECOMP_GPU=<index> picks one), needs Vulkan 1.3 and a queue that can present.
static void pick_device(void) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_vk.inst, &n, NULL);
    if (!n) FATAL(SF2C_DEVICE_S_NO_VULKAN_DEVICE_N_9110);
    VkPhysicalDevice *pd = calloc(n, sizeof *pd);
    vkEnumeratePhysicalDevices(g_vk.inst, &n, pd);
    int want = getenv(SF2C_DEVICE_S_SF2RECOMP_GPU_D219) ? atoi(getenv(SF2C_DEVICE_S_SF2RECOMP_GPU_D219)) : -1, best = -1, best_score = -1;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd[i], &p);
        LOG(SF2C_DEVICE_S_VK_GPU_U_S_API_U_U_U_N_A70D, i, p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
            VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? SF2C_DEVICE_N_3_4E07 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? SF2C_DEVICE_N_2_D473 : 1;
        if ((int)i == want) score = SF2C_DEVICE_N_100_AD57;
        if (score > best_score) { best_score = score; best = (int)i; }
    }
    if (best < 0) FATAL(SF2C_DEVICE_S_NO_VULKAN_1_3_DEVICE_N_B175);
    g_vk.phys = pd[best];
    free(pd);
    vkGetPhysicalDeviceProperties(g_vk.phys, &g_vk.props);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &nq, NULL);
    VkQueueFamilyProperties *qp = calloc(nq, sizeof *qp);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &nq, qp);
    g_vk.qfam = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.phys, i, g_vk.surface, &present);
        if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && present) {
            g_vk.qfam = i, g_vk.ts_bits = qp[i].timestampValidBits;
            break;
        }
    }
    free(qp);
    if (g_vk.qfam == UINT32_MAX) FATAL(SF2C_DEVICE_S_NO_GRAPHICS_PRESENT_QUEUE_N_0BBC);
    LOG(SF2C_DEVICE_S_VK_USING_S_N_0687, g_vk.props.deviceName);
    settings_resolve_defaults(g_vk.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
    // On by default on integrated GPUs only: lean usage and the SMAA stencil measured faster on an Intel UHD 770 and
    // slightly slower on an RTX 3080 Ti; skipping the per-frame target clears saved 3.7 ms on the UHD 770 (2x) with
    // identical images. SF2RECOMP_VK_LEAN_USAGE / SF2RECOMP_VK_SMAA_STENCIL / SF2RECOMP_NO_FRAME_CLEAR =0 / =1 override.
    bool igpu = g_vk.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_LEAN_USAGE_E9A0);
    g_vk.lean_usage = e && *e ? atoi(e) != 0 : igpu;
    e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_SMAA_STENCIL_B8BB);
    g_vk.smaa_stencil = e && *e ? atoi(e) != 0 : igpu;
    e = getenv(SF2C_DEVICE_S_SF2RECOMP_NO_FRAME_CLEAR_D7F3);
    g_vk.no_frame_clear = e && *e ? atoi(e) != 0 : igpu;
    LOG(SF2C_DEVICE_S_VK_LEAN_USAGE_S_SMAA_STENCIL_643B, g_vk.lean_usage ? SF2C_DEVICE_S_ON_990C : SF2C_DEVICE_S_OFF_9F3B,
        g_vk.smaa_stencil ? SF2C_DEVICE_S_ON_990C : SF2C_DEVICE_S_OFF_9F3B, g_vk.no_frame_clear ? SF2C_DEVICE_S_OFF_9F3B : SF2C_DEVICE_S_ON_990C, igpu ? SF2C_DEVICE_S_INTEGRATED_4DF9 : SF2C_DEVICE_S_NOT_AN_INTEGRATED_4E00);
}

static bool g_fault_ext;
// After VK_ERROR_DEVICE_LOST: what the driver says faulted (VK_EXT_device_fault), into the log.
void vk_device_fault_report(void) {
    static int once;
    if (once++ || !g_fault_ext) return;
    PFN_vkGetDeviceFaultInfoEXT get = (PFN_vkGetDeviceFaultInfoEXT)vkGetDeviceProcAddr(g_vk.dev, SF2C_DEVICE_S_VKGETDEVICEFAULTINFOEXT_22EB);
    if (!get) return;
    VkDeviceFaultCountsEXT c = {.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    if (get(g_vk.dev, &c, NULL) != VK_SUCCESS) { LOG(SF2C_DEVICE_S_VK_DEVICE_FAULT_NO_INFO_N_F17E); return; }
    VkDeviceFaultAddressInfoEXT addr[SF2C_DEVICE_N_16_B17E];
    VkDeviceFaultVendorInfoEXT vend[SF2C_DEVICE_N_16_B17E];
    if (c.addressInfoCount > SF2C_DEVICE_N_16_B17E) c.addressInfoCount = SF2C_DEVICE_N_16_B17E;
    if (c.vendorInfoCount > SF2C_DEVICE_N_16_B17E) c.vendorInfoCount = SF2C_DEVICE_N_16_B17E;
    c.vendorBinarySize = 0;
    VkDeviceFaultInfoEXT info = {.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, .pAddressInfos = addr, .pVendorInfos = vend};
    if (get(g_vk.dev, &c, &info) != VK_SUCCESS && 0) return;
    LOG(SF2C_DEVICE_S_VK_DEVICE_FAULT_S_N_E1AB, info.description);
    static const char *const kind[] = {SF2C_DEVICE_S_NONE_30A0, SF2C_DEVICE_S_READ_INVALID_1AF9, SF2C_DEVICE_S_WRITE_INVALID_B013, SF2C_DEVICE_S_EXECUTE_INVALID_ED00, SF2C_DEVICE_S_IP_UNKNOWN_A9EB, SF2C_DEVICE_S_IP_INVALID_4D17, SF2C_DEVICE_S_IP_VALID_708B};
    for (uint32_t i = 0; i < c.addressInfoCount; i++)
        LOG(SF2C_DEVICE_S_VK_ADDRESS_S_LLX_PRECISION_L_33D9, addr[i].addressType < SF2C_DEVICE_N_7_7902 ? kind[addr[i].addressType] : SF2C_DEVICE_S_EMPTY_15C5,
            (unsigned long long)addr[i].reportedAddress, (unsigned long long)addr[i].addressPrecision);
    for (uint32_t i = 0; i < c.vendorInfoCount; i++)
        LOG(SF2C_DEVICE_S_VK_VENDOR_S_CODE_LLX_DATA_LL_170A, vend[i].description, (unsigned long long)vend[i].vendorFaultCode,
            (unsigned long long)vend[i].vendorFaultData);
}

// Each memory heap: its size, the driver's budget for this process (smaller when other programs hold that memory),
// this process's use and the part LAS Recomp allocated through VMA. Logged at start and when an allocation fails
// (issue #45: out of device memory on a 6 GB GTX 1060 - our own use, or other programs'?).
void vk_log_memory(const char *why) {
    if (!g_vk.vma) return;
    const VkPhysicalDeviceMemoryProperties *mp;
    vmaGetMemoryProperties(g_vk.vma, &mp);
    VmaBudget b[VK_MAX_MEMORY_HEAPS];
    vmaGetHeapBudgets(g_vk.vma, b);
    for (uint32_t i = 0; i < mp->memoryHeapCount; i++)
        LOG(SF2C_DEVICE_S_VK_MEMORY_S_HEAP_U_S_LLU_MB__04FC, why, i,
            mp->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? SF2C_DEVICE_S_DEVICE_LOCAL_C525 : SF2C_DEVICE_S_SYSTEM_0DC8,
            (unsigned long long)(mp->memoryHeaps[i].size >> SF2C_DEVICE_N_20_F5CA), (unsigned long long)(b[i].budget >> SF2C_DEVICE_N_20_F5CA),
            (unsigned long long)(b[i].usage >> SF2C_DEVICE_N_20_F5CA), (unsigned long long)(b[i].statistics.allocationBytes >> SF2C_DEVICE_N_20_F5CA),
            b[i].statistics.allocationCount);
}

// ---- pipeline cache file
#define PCACHE_FILE SF2C_DEVICE_PCACHE_FILE
#define PCACHE_TAG SF2C_DEVICE_PCACHE_TAG
static size_t pcache_saved;  // bytes of the last save (or load)
void vk_pipeline_cache_save(void);

static void pipeline_cache_tag(char *out, size_t n) {
    char gpu[SF2C_DEVICE_N_400_26D2], uuid[SF2C_DEVICE_N_2_D473 * VK_UUID_SIZE + 1];
    for (int i = 0; i < VK_UUID_SIZE; i++) snprintf(uuid + SF2C_DEVICE_N_2_D473 * i, SF2C_DEVICE_N_3_4E07, SF2C_DEVICE_S_02X_5FB1, g_vk.props.pipelineCacheUUID[i]);
    snprintf(gpu, sizeof gpu, SF2C_DEVICE_S_S_VENDOR_X_DEVICE_X_DRIVER_U_2887, g_vk.props.deviceName, g_vk.props.vendorID,
             g_vk.props.deviceID, g_vk.props.driverVersion, uuid);
    shader_cache_tag(out, n, gpu);
}

static void *pipeline_cache_saver(void *a) {  // every 3 minutes, when it grew
    (void)a;
    for (;;) {
        SDL_Delay(SF2C_DEVICE_N_180000_C3C8);
        if (rq_stopping()) break;  // (quitting: backend_quit saves it)
        vk_pipeline_cache_save();
    }
    return NULL;
}

static void create_device(void) {
    uint32_t ne = 0;
    vkEnumerateDeviceExtensionProperties(g_vk.phys, NULL, &ne, NULL);
    VkExtensionProperties *ep = calloc(ne, sizeof *ep);
    vkEnumerateDeviceExtensionProperties(g_vk.phys, NULL, &ne, ep);
    const char *ext[SF2C_DEVICE_N_20_F5CA];
    uint32_t n_ext = 0;
    ext[n_ext++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    // Mandatory whenever advertised (MoltenVK). Use the string because this
    // extension's header declarations are guarded by VK_ENABLE_BETA_EXTENSIONS.
    if (has_ext(ep, ne, SF2C_DEVICE_S_VK_KHR_PORTABILITY_SUBSET_3197)) ext[n_ext++] = SF2C_DEVICE_S_VK_KHR_PORTABILITY_SUBSET_3197;
    g_vk.custom_border = has_ext(ep, ne, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
    if (g_vk.custom_border) ext[n_ext++] = VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME;
    g_vk.divisor = has_ext(ep, ne, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
    if (g_vk.divisor) ext[n_ext++] = VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
    const char *heap_mode = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_GUEST_HEAP_32E0);
    bool small_heap = heap_mode && !strcmp(heap_mode, SF2C_DEVICE_S_MANAGED_9C72);
    g_vk.staged_heap = heap_mode && !strcmp(heap_mode, SF2C_DEVICE_S_STAGED_B47A);
    if (heap_mode && !small_heap && !g_vk.staged_heap)
        FATAL(SF2C_DEVICE_S_INVALID_SF2RECOMP_VK_GUEST_HE_BCE7, heap_mode);
    const char *managed = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_MANAGED_HEAP_29F9);
#ifdef __linux__
    g_vk.managed_heap = managed ? atoi(managed) != 0 : strstr(g_vk.props.deviceName, SF2C_DEVICE_S_RADV_6945) != NULL;
    g_vk.managed_heap = small_heap || g_vk.managed_heap;
#else
    if (small_heap) FATAL(SF2C_DEVICE_S_SF2RECOMP_VK_GUEST_HEAP_MANAG_B46B);
    g_vk.managed_heap = managed && atoi(managed) != 0;
#endif
    g_vk.managed_heap = g_vk.managed_heap && !g_vk.staged_heap &&
                        !getenv(SF2C_DEVICE_S_SF2RECOMP_VK_NO_GMEM_2D48);
    g_vk.host_import = !g_vk.managed_heap && !g_vk.staged_heap && has_ext(ep, ne, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    if (g_vk.managed_heap) LOG(SF2C_DEVICE_S_VK_GUEST_HEAP_DRIVER_MANAGED_03F5);
    if (g_vk.staged_heap) LOG(SF2C_DEVICE_S_VK_GUEST_HEAP_STAGED_UPLOADS_47BC);
    if (g_vk.host_import) ext[n_ext++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    bool fault = has_ext(ep, ne, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);  // device-lost diagnostics (vk_device_fault_report)
    if (fault) ext[n_ext++] = VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
    g_fault_ext = fault;
    bool budget = has_ext(ep, ne, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);  // (real per-heap budgets: vk_log_memory)
    if (budget) ext[n_ext++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    // pipeline-building features: logged below (issue #92); dynamic vertex input used, the library one only measured
    bool vids_ext = has_ext(ep, ne, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
#ifdef VK_EXT_graphics_pipeline_library
    bool gpl_ext = has_ext(ep, ne, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) &&
                   has_ext(ep, ne, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
#endif
#ifdef VK_EXT_extended_dynamic_state3
    bool eds3_ext = has_ext(ep, ne, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
#endif
#ifdef __linux__
    // the guest heap as driver memory mapped at HEAP_BASE (exec.c backend_guest_heap_prepare)
    if (g_vk.managed_heap) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
        g_vk.managed_heap_size = managed_heap_size(&mp, small_heap);
    }
    g_vk.dmabuf = !g_vk.staged_heap && has_ext(ep, ne, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
                  has_ext(ep, ne, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    if (g_vk.dmabuf) {
        ext[n_ext++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        ext[n_ext++] = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
    }
    // ... mapped there by the driver itself (its own DRM file, as every vkMapMemory), not through the dma-buf (issue #92)
    // (older Vulkan headers, e.g. a distro's, lack it: then the dma-buf mapping only)
#ifdef VK_EXT_map_memory_placed
    bool placed_ext = !g_vk.staged_heap && has_ext(ep, ne, VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME) &&
                      has_ext(ep, ne, VK_KHR_MAP_MEMORY_2_EXTENSION_NAME);
#endif
#endif
    free(ep);

    // features the backend relies on (all present on current desktop drivers)
    VkPhysicalDeviceVulkan12Features have12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 have = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &have12};
#if defined(__linux__) && defined(VK_EXT_map_memory_placed)
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT have_placed = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT};
    if (placed_ext) have12.pNext = &have_placed;
#endif
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT have_vids = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT};
    if (vids_ext) have_vids.pNext = have.pNext, have.pNext = &have_vids;
#ifdef VK_EXT_graphics_pipeline_library
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT have_gpl = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
    if (gpl_ext) have_gpl.pNext = have.pNext, have.pNext = &have_gpl;
#endif
#ifdef VK_EXT_extended_dynamic_state3
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT have_eds3 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
    if (eds3_ext) have_eds3.pNext = have.pNext, have.pNext = &have_eds3;
#endif
    vkGetPhysicalDeviceFeatures2(g_vk.phys, &have);
    {   // what the driver offers for building pipelines (one line, for #92 reports)
        VkPhysicalDeviceDriverProperties drv = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 pr = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv};
        char gpl[SF2C_DEVICE_N_96_7B1A] = SF2C_DEVICE_S_NO_EXTENSION_DF67, eds3[SF2C_DEVICE_N_160_A512] = SF2C_DEVICE_S_NO_EXTENSION_DF67;
#ifdef VK_EXT_graphics_pipeline_library
        VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT gplp = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT};
        if (gpl_ext) gplp.pNext = pr.pNext, pr.pNext = &gplp;
#else
        snprintf(gpl, sizeof gpl, SF2C_DEVICE_S_NOT_IN_THIS_BUILD_S_HEADERS_745F);
#endif
#ifndef VK_EXT_extended_dynamic_state3
        snprintf(eds3, sizeof eds3, SF2C_DEVICE_S_NOT_IN_THIS_BUILD_S_HEADERS_745F);
#endif
        vkGetPhysicalDeviceProperties2(g_vk.phys, &pr);
#ifdef VK_EXT_graphics_pipeline_library
        if (gpl_ext)
            snprintf(gpl, sizeof gpl, SF2C_DEVICE_S_S_FAST_LINKING_S_INDEPENDENT_C63E,
                     have_gpl.graphicsPipelineLibrary ? SF2C_DEVICE_S_YES_6C76 : SF2C_DEVICE_S_NO_04A0, gplp.graphicsPipelineLibraryFastLinking ? SF2C_DEVICE_S_YES_6C76 : SF2C_DEVICE_S_NO_04A0,
                     gplp.graphicsPipelineLibraryIndependentInterpolationDecoration ? SF2C_DEVICE_S_YES_6C76 : SF2C_DEVICE_S_NO_04A0);
#endif
#ifdef VK_EXT_extended_dynamic_state3
        if (eds3_ext)
            snprintf(eds3, sizeof eds3, SF2C_DEVICE_S_POLYGON_MODE_D_SAMPLES_D_ALP_0E29
                     SF2C_DEVICE_S_BLEND_EQUATION_D_WRITE_MASK__C2B7, have_eds3.extendedDynamicState3PolygonMode,
                     have_eds3.extendedDynamicState3RasterizationSamples, have_eds3.extendedDynamicState3AlphaToCoverageEnable,
                     have_eds3.extendedDynamicState3ColorBlendEnable, have_eds3.extendedDynamicState3ColorBlendEquation,
                     have_eds3.extendedDynamicState3ColorWriteMask);
#endif
        LOG(SF2C_DEVICE_S_VK_DRIVER_S_S_GRAPHICS_PIPEL_6E0F,
            drv.driverName, drv.driverInfo, gpl, vids_ext ? (have_vids.vertexInputDynamicState ? SF2C_DEVICE_S_YES_6C76 : SF2C_DEVICE_S_NO_04A0) : SF2C_DEVICE_S_NO_EXTENSION_DF67,
            eds3);
    }
    {   // dynamic vertex input (exec.c): fewer pipelines; SF2RECOMP_VK_DYN_VERTEX=0 keeps the layout in each pipeline
        const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_DYN_VERTEX_0922);
        bool off = e && *e && !atoi(e);
        // Intel's frozen Windows driver for its older integrated GPUs (UHD 630, Ice Lake...: Vulkan 1.3.215 and older)
        // draws nothing with it - a black picture (#8, #357); SF2RECOMP_VK_DYN_VERTEX=1 still turns it on there
        bool intel_legacy = false;
#ifdef _WIN32
        intel_legacy = g_vk.props.vendorID == SF2C_DEVICE_N_0X8086_D5E6 && g_vk.props.apiVersion <= VK_MAKE_API_VERSION(0, 1, SF2C_DEVICE_N_3_4E07, SF2C_DEVICE_N_215_D865);
#endif
        if (intel_legacy && !(e && *e)) off = true;
        g_vk.vids = vids_ext && have_vids.vertexInputDynamicState && !off;
        if (g_vk.vids) ext[n_ext++] = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
        else LOG(SF2C_DEVICE_S_VK_VERTEX_INPUT_IN_EACH_PIPE_BCEA,
                 intel_legacy && !(e && *e) ? SF2C_DEVICE_S_INTEL_S_OLDER_WINDOWS_DRIVER_8E1D :
                 off ? SF2C_DEVICE_S_SF2RECOMP_VK_DYN_VERTEX_0_7B69 :
                 !vids_ext ? SF2C_DEVICE_S_NO_VK_EXT_VERTEX_INPUT_DYNAM_300C : SF2C_DEVICE_S_VERTEXINPUTDYNAMICSTATE_NOT__E76B);
    }
#ifdef VK_EXT_graphics_pipeline_library
    {   // SF2RECOMP_VK_GPL_PROBE=1: library builds timed after shader preparation (exec.c gpl_probe); nothing else uses it
        const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_GPL_PROBE_6BED);
        if (e && atoi(e)) {
            g_vk.gpl_probe = gpl_ext && have_gpl.graphicsPipelineLibrary;
            if (g_vk.gpl_probe) {
                ext[n_ext++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME;
                ext[n_ext++] = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME;
            } else LOG(SF2C_DEVICE_S_GPL_PROBE_THE_DRIVER_HAS_NO__0DBA);
        }
    }
#else
    if (getenv(SF2C_DEVICE_S_SF2RECOMP_VK_GPL_PROBE_6BED) && atoi(getenv(SF2C_DEVICE_S_SF2RECOMP_VK_GPL_PROBE_6BED))) LOG(SF2C_DEVICE_S_GPL_PROBE_THIS_BUILD_S_VULKA_3E51);
#endif
#if defined(__linux__) && defined(VK_EXT_map_memory_placed)
    have12.pNext = NULL;
    g_vk.map_placed = placed_ext && have_placed.memoryMapPlaced;
    g_vk.unmap_reserve = g_vk.map_placed && have_placed.memoryUnmapReserve;
    if (g_vk.map_placed) {
        ext[n_ext++] = VK_KHR_MAP_MEMORY_2_EXTENSION_NAME;
        ext[n_ext++] = VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
    }
#endif
    bool probe;
    g_vk.lazy_heap = lazy_heap_predicted(budget, &probe);
    g_vk.heap_probe = !g_vk.lazy_heap && probe && g_vk.host_import && have12.bufferDeviceAddress;
    g_vk.bda = (g_vk.lazy_heap || g_vk.heap_probe) && have12.bufferDeviceAddress;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT div = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
        .vertexAttributeInstanceRateDivisor = VK_TRUE, .vertexAttributeInstanceRateZeroDivisor = VK_FALSE};
    VkPhysicalDeviceCustomBorderColorFeaturesEXT cbc = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, .customBorderColors = VK_TRUE,
        .customBorderColorWithoutFormat = VK_TRUE, .pNext = g_vk.divisor ? &div : NULL};
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                            .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE,
                                            .shaderDemoteToHelperInvocation = VK_TRUE, .shaderTerminateInvocation = VK_TRUE,
                                            .maintenance4 = VK_TRUE,
                                            .pNext = g_vk.custom_border ? (void *)&cbc : g_vk.divisor ? (void *)&div : NULL};
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .timelineSemaphore = VK_TRUE, .shaderInt8 = VK_TRUE, .samplerMirrorClampToEdge = VK_TRUE,
                                            // Adreno exposes storage-buffer byte access but may omit the
                                            // optional uniform-buffer equivalent. Our UBOs use 32-bit words.
                                            .uniformAndStorageBuffer8BitAccess = have12.uniformAndStorageBuffer8BitAccess,
                                            .storageBuffer8BitAccess = VK_TRUE,
                                            .scalarBlockLayout = VK_TRUE, .bufferDeviceAddress = g_vk.bda, .pNext = &f13};
    VkPhysicalDeviceVulkan11Features f11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                            .storageBuffer16BitAccess = VK_TRUE, .pNext = &f12};
    VkPhysicalDeviceFaultFeaturesEXT ff = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT, .deviceFault = VK_TRUE,
                                           .pNext = &f11};
    VkPhysicalDeviceFeatures2 f = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = fault ? (void *)&ff : (void *)&f11};
#if defined(__linux__) && defined(VK_EXT_map_memory_placed)
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placed = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT,
                                                         .memoryMapPlaced = VK_TRUE, .memoryUnmapReserve = g_vk.unmap_reserve,
                                                         .pNext = f.pNext};
    if (g_vk.map_placed) f.pNext = &placed;
#endif
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT en_vids = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, .vertexInputDynamicState = VK_TRUE};
    if (g_vk.vids) en_vids.pNext = f.pNext, f.pNext = &en_vids;
#ifdef VK_EXT_graphics_pipeline_library
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT en_gpl = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT, .graphicsPipelineLibrary = VK_TRUE};
    if (g_vk.gpl_probe) en_gpl.pNext = f.pNext, f.pNext = &en_gpl;
#endif
    VkPhysicalDeviceFeatures *b = &f.features, *h = &have.features;
    b->independentBlend = h->independentBlend;
    b->dualSrcBlend = h->dualSrcBlend;
    b->fillModeNonSolid = h->fillModeNonSolid;
    b->samplerAnisotropy = h->samplerAnisotropy;
    b->textureCompressionBC = h->textureCompressionBC;
    b->shaderInt64 = h->shaderInt64;
    b->depthBiasClamp = h->depthBiasClamp;
    b->depthClamp = h->depthClamp;
    b->largePoints = h->largePoints;
    b->wideLines = h->wideLines;
    b->imageCubeArray = h->imageCubeArray;
    b->sampleRateShading = h->sampleRateShading;
    b->fragmentStoresAndAtomics = h->fragmentStoresAndAtomics;
    b->vertexPipelineStoresAndAtomics = h->vertexPipelineStoresAndAtomics;
    b->robustBufferAccess = h->robustBufferAccess;  // guest shaders index cbufs freely: keep OOB reads harmless
    g_vk.ssbo_chunks = h->fragmentStoresAndAtomics && h->vertexPipelineStoresAndAtomics && h->robustBufferAccess;
    b->shaderClipDistance = h->shaderClipDistance;
    if (!h->shaderInt64) LOG(SF2C_DEVICE_S_VK_WARNING_NO_SHADERINT64_64_B6B2);
    float prio = SF2C_DEVICE_N_1_0F_CA71;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = g_vk.qfam,
                                  .queueCount = 1, .pQueuePriorities = &prio};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qi, .enabledExtensionCount = n_ext, .ppEnabledExtensionNames = ext};
    VK_CHECK(vkCreateDevice(g_vk.phys, &di, NULL, &g_vk.dev));
    vkGetDeviceQueue(g_vk.dev, g_vk.qfam, 0, &g_vk.queue);
    if (g_vk.vids) {
        g_vk.set_vertex_input = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(g_vk.dev, SF2C_DEVICE_S_VKCMDSETVERTEXINPUTEXT_2217);
        if (!g_vk.set_vertex_input) g_vk.vids = false;
        LOG(SF2C_DEVICE_S_VK_VERTEX_INPUT_S_N_C50A, g_vk.vids ? SF2C_DEVICE_S_DYNAMIC_VK_EXT_VERTEX_INPUT__B512 :
            SF2C_DEVICE_S_IN_EACH_PIPELINE_VKCMDSETVER_8433);
    }

    VmaAllocatorCreateInfo ai = {.physicalDevice = g_vk.phys, .device = g_vk.dev, .instance = g_vk.inst,
                                 .vulkanApiVersion = VK_API_VERSION_1_3,
                                 .flags = budget ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0};
    VK_CHECK(vmaCreateAllocator(&ai, &g_vk.vma));
    vk_log_memory(SF2C_DEVICE_S_START_A92A);

    VkSemaphoreTypeCreateInfo st = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &st};
    VK_CHECK(vkCreateSemaphore(g_vk.dev, &si, NULL, &g_vk.timeline));
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = g_vk.qfam};
    VK_CHECK(vkCreateCommandPool(g_vk.dev, &pi, NULL, &g_vk.pool));

    // pipeline cache: cache/vk_pipelines.bin (written by vk_pipeline_cache_save), used only with its tag
    // (vk_pipelines.bin.tag: this build, the translator, the GPU and its driver - another one starts it empty)
    char tag[SF2C_DEVICE_N_512_94F8];
    pipeline_cache_tag(tag, sizeof tag);
    size_t sz = 0;
    void *data = NULL;
    FILE *fp = shader_cache_tag_ok(PCACHE_TAG, tag) ? fopen(PCACHE_FILE, SF2C_DEVICE_S_RB_46CA) : NULL;
    if (!fp && !access(PCACHE_FILE, F_OK)) LOG(SF2C_DEVICE_S_VK_PIPELINE_CACHE_FROM_ANOTH_FB0F);
    if (fp) {
        fseek(fp, 0, SEEK_END);
        sz = (size_t)ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = malloc(sz);
        if (fread(data, 1, sz, fp) != sz) sz = 0;
        fclose(fp);
    }
    VkPipelineCacheCreateInfo pc = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, .initialDataSize = sz,
                                    .pInitialData = data};
    if (vkCreatePipelineCache(g_vk.dev, &pc, NULL, &g_vk.pcache) != VK_SUCCESS) {
        pc.initialDataSize = 0;
        VK_CHECK(vkCreatePipelineCache(g_vk.dev, &pc, NULL, &g_vk.pcache));
    }
    free(data);
    if (pc.initialDataSize) LOG(SF2C_DEVICE_S_VK_PIPELINE_CACHE_LOADED_ZU__127F, pc.initialDataSize);
    pcache_saved = pc.initialDataSize;
    pthread_t t;  // (kept every few minutes while it grows, not only at a clean exit)
    pthread_create(&t, NULL, pipeline_cache_saver, NULL);
    pthread_detach(t);
}

// Saved atomically (temp file + rename: a crash mid-write leaves the previous one), with its tag; one save at a time
void vk_pipeline_cache_save(void) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    size_t sz = 0;
    if (vkGetPipelineCacheData(g_vk.dev, g_vk.pcache, &sz, NULL) == VK_SUCCESS && sz && sz != pcache_saved) {
        void *data = malloc(sz);
        if (vkGetPipelineCacheData(g_vk.dev, g_vk.pcache, &sz, data) == VK_SUCCESS && shader_cache_write(PCACHE_FILE, data, sz)) {
            char tag[SF2C_DEVICE_N_512_94F8];
            pipeline_cache_tag(tag, sizeof tag);
            if (!shader_cache_tag_ok(PCACHE_TAG, tag)) shader_cache_tag_write(PCACHE_TAG, tag);
            pcache_saved = sz;
            LOG(SF2C_DEVICE_S_VK_PIPELINE_CACHE_SAVED_ZU_B_6C21, sz);
        }
        free(data);
    }
    pthread_mutex_unlock(&lock);
}

// ================================================================== swapchain
static VkPresentModeKHR present_mode(void) {
    // MAILBOX by default: NVN presentation enforces the configured frame period, and with FIFO every submission
    // queued behind the one waiting for a window image, so the game's fence waits followed the presentation engine:
    // a throttled / captured / busy compositor slowed the whole game (4-21 fps with the GPU mostly idle; mailbox:
    // 60). No tearing. SF2RECOMP_VK_PRESENT=fifo|immediate|mailbox to choose.
    const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_PRESENT_0D5A);
    VkPresentModeKHR want = !e || !strcmp(e, SF2C_DEVICE_S_MAILBOX_69E3) ? VK_PRESENT_MODE_MAILBOX_KHR
                          : !strcmp(e, SF2C_DEVICE_S_IMMEDIATE_6006) ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.phys, g_vk.surface, &n, NULL);
    VkPresentModeKHR m[SF2C_DEVICE_N_16_B17E];
    if (n > SF2C_DEVICE_N_16_B17E) n = SF2C_DEVICE_N_16_B17E;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.phys, g_vk.surface, &n, m);
    for (uint32_t i = 0; i < n; i++) if (m[i] == want) return want;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static void create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.phys, g_vk.surface, &caps));
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    VkExtent2D ext = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){(uint32_t)w, (uint32_t)h};
    if (!ext.width || !ext.height) return;  // minimised
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.phys, g_vk.surface, &nf, NULL);
    VkSurfaceFormatKHR fmts[SF2C_DEVICE_N_64_A68B];
    if (nf > SF2C_DEVICE_N_64_A68B) nf = SF2C_DEVICE_N_64_A68B;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.phys, g_vk.surface, &nf, fmts);
    // scanout shows the bytes the game wrote (see the raw views in the present pass): a UNORM swapchain like Metal's
    VkSurfaceFormatKHR sf = fmts[0];
    for (uint32_t i = 0; i < nf; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = fmts[i]; break; }
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > SF2C_DEVICE_N_8_2C62) count = SF2C_DEVICE_N_8_2C62;
    VkSwapchainKHR old = g_vk.swapchain;
#ifdef __ANDROID__
    // The final pass renders in SDL window coordinates; it does not pre-rotate
    // its vertices for the device's natural display orientation.
    VkSurfaceTransformFlagBitsKHR transform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    g_vk.compositor_rotation = transform != caps.currentTransform;
    LOG(SF2C_DEVICE_S_ANDROID_SURFACE_WINDOW_DX_D__038A,
        w, h, ext.width, ext.height, caps.supportedTransforms, caps.currentTransform, transform);
#else
    VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
#endif
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = g_vk.surface, .minImageCount = count,
        .imageFormat = sf.format, .imageColorSpace = sf.colorSpace, .imageExtent = ext, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = transform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = present_mode(), .clipped = VK_TRUE,
        .oldSwapchain = old};
    VK_CHECK(vkCreateSwapchainKHR(g_vk.dev, &ci, NULL, &g_vk.swapchain));
    static const char *const modes[] = {SF2C_DEVICE_S_IMMEDIATE_6006, SF2C_DEVICE_S_MAILBOX_69E3, SF2C_DEVICE_S_FIFO_85C6, SF2C_DEVICE_S_FIFO_RELAXED_0BCD};
    LOG(SF2C_DEVICE_S_VK_SWAPCHAIN_UX_U_U_IMAGES_S_9EA9, ext.width, ext.height, count,
        ci.presentMode < SF2C_DEVICE_N_4_4B22 ? modes[ci.presentMode] : SF2C_DEVICE_S_OTHER_PRESENT_MODE_5564);
    for (uint32_t i = 0; i < g_vk.sc_count; i++) vkDestroyImageView(g_vk.dev, g_vk.sc_views[i], NULL);
    if (old) vkDestroySwapchainKHR(g_vk.dev, old, NULL);
    g_vk.sc_format = sf.format;
    g_vk.sc_extent = ext;
    g_vk.sc_count = SF2C_DEVICE_N_8_2C62;
    vkGetSwapchainImagesKHR(g_vk.dev, g_vk.swapchain, &g_vk.sc_count, g_vk.sc_images);
    for (uint32_t i = 0; i < g_vk.sc_count; i++) {
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = g_vk.sc_images[i],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = sf.format,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VK_CHECK(vkCreateImageView(g_vk.dev, &vi, NULL, &g_vk.sc_views[i]));
    }
}

static void recreate_swapchain(void) {
    rec_drain();  // (every queued present and submission made: the queue is idle for the wait)
    vkDeviceWaitIdle(g_vk.dev);
    create_swapchain();
}

// Time this thread spends waiting here - for a frame slot's GPU work, a window image, the presentation engine -
// counts as idle for the frame pacer (pace.c), like the game's own waits: in FIFO (exclusive fullscreen
// on Windows offers no mailbox) acquire / present block until the display's refresh, and counted as busy that made
// every frame look ~16.6 ms: the game's 30 fps mode outdoors on a fast GPU (issue #44). GPU-bound frames still show
// in the game's GPU counters.
extern __thread uint64_t g_blocked_ns;  // kernel.c
#define WAITING(x) do { uint64_t w0_ = plat_now_ns(); rq_watch(#x, 0); x; rq_watch(NULL, 0); g_blocked_ns += plat_now_ns() - w0_; } while (0)
// the same, also counted by place (dynamic resolution's log / SF2RECOMP_VK_PROF: 0 frame slot, 1 acquire, 2 present)
uint64_t g_vk_frame_waits[SF2C_DEVICE_N_4_4B22];
#define WAITING_AT(i, x) do { uint64_t w0_ = plat_now_ns(); rq_watch(#x, 0); x; rq_watch(NULL, 0); uint64_t w1_ = plat_now_ns() - w0_; \
                              g_blocked_ns += w1_; g_vk_frame_waits[i] += w1_; } while (0)

// ================================================================== frames (start screen and game)
// Acquire semaphores rotate per frame in flight; a slot is reused once the submission that waited on it is done.
#define FRAMES SF2C_DEVICE_FRAMES
static VkSemaphore acquire_sem[FRAMES], done_sem[SF2C_DEVICE_N_8_2C62];
static uint64_t slot_seq[FRAMES];
static int slot;

static VkSemaphore binary_semaphore(void) {
    VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s;
    VK_CHECK(vkCreateSemaphore(g_vk.dev, &si, NULL, &s));
    return s;
}

static void image_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                               .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                               .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                               .oldLayout = from, .newLayout = to, .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b};
    rec_CmdPipelineBarrier2(cb, &di);
}

// id: frames acquired so far; secondary: the window image's rendering open now is the overlay's (vk_overlay_end), for
// secondary command buffers only
typedef struct { uint32_t image; VkSemaphore acquired; bool ok, secondary; uint64_t id; } Frame;
static Frame cur_frame;

// Acquire the next window image and begin rendering into it (cleared to `clear`). Returns false when there is
// nothing to draw into (minimised window).
bool vk_frame_begin(const float clear[SF2C_DEVICE_N_4_4B22]) {
    cur_frame.ok = false;
    if (rq_stopping()) return false;  // quitting: no window image after the window closed (#40)
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (!w || !h) return false;  // minimised
    // The last present (the recorder's, record.c) must be done before the swapchain is used again - acquire and present
    // on the same swapchain are not allowed at the same time - and it may have asked for a new swapchain
    WAITING(rec_wait_presents(rec_presents_queued()));
    if (rec_present_recreate()) recreate_swapchain();
    if (!g_vk.swapchain || (uint32_t)w != g_vk.sc_extent.width || (uint32_t)h != g_vk.sc_extent.height) recreate_swapchain();
    if (!g_vk.swapchain) return false;
    static int trace = -1;
    if (trace < 0) trace = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_TRACE_B60E) != NULL;
    if (trace) LOG(SF2C_DEVICE_S_VK_FRAME_BEGIN_DX_D_SWAPCHAI_7589, w, h, g_vk.sc_extent.width, g_vk.sc_extent.height);
    slot = (slot + 1) % FRAMES;
    if (!acquire_sem[slot]) acquire_sem[slot] = binary_semaphore();
    WAITING_AT(0, vk_wait_seq(slot_seq[slot]));
    VkResult r;
    WAITING_AT(1, r = vkAcquireNextImageKHR(g_vk.dev, g_vk.swapchain, UINT64_MAX, acquire_sem[slot], VK_NULL_HANDLE, &cur_frame.image));
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        WAITING_AT(1, r = vkAcquireNextImageKHR(g_vk.dev, g_vk.swapchain, UINT64_MAX, acquire_sem[slot], VK_NULL_HANDLE,
                                          &cur_frame.image));
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { LOG(SF2C_DEVICE_S_VK_ACQUIRE_FAILED_D_N_9B2A, r); return false; }
    cur_frame.acquired = acquire_sem[slot];
    cur_frame.id++;
    cur_frame.secondary = false;
    slot_seq[slot] = vk_pending_seq();
    VkCommandBuffer cb = vk_cmd();
    // The output pass samples the game's render target from an earlier
    // submission. The swapchain image barrier only covers the window image;
    // make the game's colour writes visible to these shader reads as well.
    vk_full_barrier(cb);
    image_barrier(cb, g_vk.sc_images[cur_frame.image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vk_gpu_timer_window_start();  // (the GPU timer: this submission's work starts after that barrier, which waits for the image)
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = g_vk.sc_views[cur_frame.image],
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                    .clearValue.color.float32 = {clear[0], clear[1], clear[SF2C_DEVICE_N_2_D473], clear[SF2C_DEVICE_N_3_4E07]}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, g_vk.sc_extent}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    rec_CmdBeginRendering(cb, &ri);
    VkViewport vp = {0, 0, (float)g_vk.sc_extent.width, (float)g_vk.sc_extent.height, 0, 1};
    VkRect2D sc = {{0, 0}, g_vk.sc_extent};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    cur_frame.ok = true;
    return true;
}

// Passes of their own between vk_frame_begin and vk_frame_end (exec.c post passes): end the window image's
// rendering, and begin it again keeping what was drawn.
void vk_frame_suspend(void) {
    if (cur_frame.ok) rec_CmdEndRendering(vk_cmd());
}
void vk_frame_resume(void) {
    if (!cur_frame.ok) return;
    cur_frame.secondary = false;
    VkCommandBuffer cb = vk_cmd();
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = g_vk.sc_views[cur_frame.image],
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, g_vk.sc_extent}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    rec_CmdBeginRendering(cb, &ri);
    VkViewport vp = {0, 0, (float)g_vk.sc_extent.width, (float)g_vk.sc_extent.height, 0, 1};
    VkRect2D sc = {{0, 0}, g_vk.sc_extent};
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
}

VkExtent2D vk_frame_extent(void) { return g_vk.sc_extent; }
// The window image's command buffer for direct recording (the start screen's ImGui): the recorder has recorded
// everything queued before it, and records what is queued after it.
void *vk_frame_cmd(void) {
    VkCommandBuffer cb = vk_cmd();
    rec_drain();
    return (void *)cb;
}

// ---- the overlay (menu.mm menu_render: ImGui's FPS counter, run timer, cards, the menu)
// ImGui's backend (imgui_impl_vulkan.cpp) makes its vkCmd* calls itself, on the thread that presents. With the recorder
// thread on (record.c), the window image's command buffer and its pool are the recorder's until it has caught up:
// ImGui recording into that buffer needed a full rec_drain first, every frame an overlay was up, which serialised the
// two threads (the render thread waited for the recorder to record the whole frame: ~5 ms a frame on a desktop, 20-30
// on a Steam Deck with only the FPS counter shown). Now ImGui records into a secondary command buffer from a pool of
// this file's - one per frame slot, never recorded by the recorder - and the recorder executes it in its place in the
// window image's commands: the window's rendering ends, a rendering of secondary command buffers begins on the same
// image (its contents loaded) and executes it. That rendering stays open for vk_frame_end to end: nothing is drawn
// into the window image inline after the overlay.
// A slot's buffer is recorded again (its pool reset) only once the GPU finished the submission that executed it - the
// slot comes round after vk_frame_begin waited for the window submission three frames back, so normally that is
// already so - which implies the recorder made that vkCmdExecuteCommands long before: the pool is this thread's alone
// whenever it touches it. ImGui's own objects (pipeline, buffers, descriptor sets) were only ever used by this thread.
// The recorder off, SF2RECOMP_VK_OVERLAY_DIRECT=1, or a second overlay in one frame: ImGui records into the window
// image's command buffer itself, after rec_drain (a no-op with the recorder off), as before.
static VkCommandPool ov_pool[FRAMES];
static VkCommandBuffer ov_cb[FRAMES];
static uint64_t ov_seq[FRAMES], ov_frame[FRAMES];  // the submission that executed the slot's buffer; the frame it was for
static VkCommandBuffer ov_open;                    // (handed out by vk_overlay_begin, until vk_overlay_end)
static int ov_direct = -1;

static bool ov_slot_ready(void) {  // (the slot's pool and buffer exist, and the buffer is free to record)
    if (!ov_pool[slot]) {
        VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                      .queueFamilyIndex = g_vk.qfam};
        if (vkCreateCommandPool(g_vk.dev, &pi, NULL, &ov_pool[slot]) != VK_SUCCESS) { ov_pool[slot] = VK_NULL_HANDLE; return false; }
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ov_pool[slot],
                                          .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY, .commandBufferCount = 1};
        if (vkAllocateCommandBuffers(g_vk.dev, &ai, &ov_cb[slot]) != VK_SUCCESS) {
            vkDestroyCommandPool(g_vk.dev, ov_pool[slot], NULL);
            ov_pool[slot] = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }
    if (ov_seq[slot] > vk_completed_seq()) vk_wait_seq(ov_seq[slot]);  // (normally done already: see above)
    VK_CHECK(vkResetCommandPool(g_vk.dev, ov_pool[slot], 0));
    return true;
}

// The command buffer for ImGui to record the overlay into (window_cmd: the window image's, inside its rendering), then
// vk_overlay_end with both
void *vk_overlay_begin(void *window_cmd) {
    if (ov_direct < 0) {
        const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_OVERLAY_DIRECT_EFEC);
        ov_direct = !rec_on() || (e && atoi(e) != 0);
        LOG(SF2C_DEVICE_S_VK_OVERLAY_S_N_AA59, ov_direct ? SF2C_DEVICE_S_RECORDED_INTO_THE_WINDOW_IMA_6C34 : SF2C_DEVICE_S_A_SECONDARY_COMMAND_BUFFER_T_EF30);
    }
    if (ov_direct || !cur_frame.ok || ov_open || ov_frame[slot] == cur_frame.id || !ov_slot_ready()) {
        if (cur_frame.ok && cur_frame.secondary) {  // (a second overlay this frame: inline draws again, in a rendering of their own)
            vk_frame_suspend();
            vk_frame_resume();
        }
        rec_drain();  // (ImGui records into window_cmd itself: the recorder caught up and idle first)
        return window_cmd;
    }
    VkCommandBufferInheritanceRenderingInfo ir = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
                                                  .colorAttachmentCount = 1, .pColorAttachmentFormats = &g_vk.sc_format,
                                                  .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkCommandBufferInheritanceInfo ii = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, .pNext = &ir};
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
                                   .pInheritanceInfo = &ii};
    VK_CHECK(vkBeginCommandBuffer(ov_cb[slot], &bi));
    ov_frame[slot] = cur_frame.id;
    ov_open = ov_cb[slot];
    return (void *)ov_open;
}

void vk_overlay_end(void *window_cmd, void *cmd) {
    if (!cmd || cmd == window_cmd || (VkCommandBuffer)cmd != ov_open) return;  // (recorded into window_cmd directly)
    VkCommandBuffer sec = ov_open, cb = (VkCommandBuffer)window_cmd;
    ov_open = VK_NULL_HANDLE;
    VK_CHECK(vkEndCommandBuffer(sec));
    rec_CmdEndRendering(cb);
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = g_vk.sc_views[cur_frame.image],
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
                          .renderArea = {{0, 0}, g_vk.sc_extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &ca};
    rec_CmdBeginRendering(cb, &ri);
    rec_CmdExecuteCommands(cb, 1, &sec);
    cur_frame.secondary = true;
    ov_seq[slot] = vk_pending_seq();  // (the submission window_cmd goes in)
}

static void save_window_image(VkCommandBuffer cb, VkSpan *out) {
    uint32_t w = g_vk.sc_extent.width, h = g_vk.sc_extent.height;
    *out = vk_readback((uint64_t)w * h * SF2C_DEVICE_N_4_4B22);
    image_barrier(cb, g_vk.sc_images[cur_frame.image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy rg = {.bufferOffset = out->off, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            .imageExtent = {w, h, 1}};
    rec_CmdCopyImageToBuffer(cb, g_vk.sc_images[cur_frame.image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out->buf, 1, &rg);
    image_barrier(cb, g_vk.sc_images[cur_frame.image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

// End rendering into the window image and present it. shot_path: also save the window image as PNG.
void vk_frame_end(const char *shot_path) {
    if (!cur_frame.ok) return;
    VkCommandBuffer cb = vk_cmd();
    rec_CmdEndRendering(cb);
    VkSpan shot = {0};
    if (shot_path) save_window_image(cb, &shot);
    image_barrier(cb, g_vk.sc_images[cur_frame.image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    uint32_t img = cur_frame.image;
    if (!done_sem[img]) done_sem[img] = binary_semaphore();
    uint64_t seq = vk_pending_seq();
    vk_submit_extra(cur_frame.acquired, done_sem[img], VK_NULL_HANDLE);
    // the present follows the submission in the recorder's order (its wait for the display is the recorder's, counted in
    // g_vk_frame_waits[2]; an out-of-date result makes the next vk_frame_begin recreate the swapchain)
    rec_QueuePresentKHR(done_sem[img], g_vk.swapchain, img);
    cur_frame.ok = false;
    if (shot_path) {
        vk_wait_seq(seq);
        uint32_t w = g_vk.sc_extent.width, h = g_vk.sc_extent.height;
        uint8_t *px = shot.ptr;
        bool bgr = g_vk.sc_format == VK_FORMAT_B8G8R8A8_UNORM || g_vk.sc_format == VK_FORMAT_B8G8R8A8_SRGB;
        for (uint64_t i = 0; bgr && i < (uint64_t)w * h; i++) { uint8_t t = px[i * SF2C_DEVICE_N_4_4B22]; px[i * SF2C_DEVICE_N_4_4B22] = px[i * SF2C_DEVICE_N_4_4B22 + SF2C_DEVICE_N_2_D473]; px[i * SF2C_DEVICE_N_4_4B22 + SF2C_DEVICE_N_2_D473] = t; }
        for (uint64_t i = 0; i < (uint64_t)w * h; i++) px[i * SF2C_DEVICE_N_4_4B22 + SF2C_DEVICE_N_3_4E07] = SF2C_DEVICE_N_255_9556;
        vk_save_png(shot_path, px, w, h);
    }
}

void vk_save_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h) {
    SDL_Surface *s = SDL_CreateSurfaceFrom((int)w, (int)h, SDL_PIXELFORMAT_ABGR8888, (void *)rgba, (int)w * SF2C_DEVICE_N_4_4B22);
    if (!s || !SDL_SavePNG(s, path)) LOG(SF2C_DEVICE_S_VK_SAVING_S_FAILED_S_N_2C01, path, SDL_GetError());
    else LOG(SF2C_DEVICE_S_VK_SAVED_S_UX_U_N_9808, path, w, h);
    SDL_DestroySurface(s);
}

// ================================================================== game presentation (metal.m backend_present)
// Draws the game's image into the window with the widescreen crop, then the overlay menu.
extern void vk_present_draw(VkCommandBuffer cb, TexBackend *b, float vx, float vy, float vw, float vh, const float pv[SF2C_DEVICE_N_8_2C62]);
extern bool g_debug_frame;  // (exec.c; set in present_hooks, read in the render thread's present)
void renderer_submit(void);
void renderer_set_scale(float sx, float sy);
uint64_t renderer_present_count(void);
void renderer_save_texture_tex(TexBackend *b, const char *path);

static uint64_t frames, g_frame_counter;
static _Atomic uint64_t total_frames;
static _Atomic uint64_t last_game_present_ns;
static _Atomic(NvnTexture *) g_last_presented;
uint64_t renderer_present_count(void) { return atomic_load(&total_frames); }  // (the game thread's frame: the HLE hooks')
// The frame whose work is being translated (rqueue.h rq_frame): "once per frame" state of the translation. Without the
// render thread, total_frames.
uint64_t renderer_exec_frame(void) { return rq_current_item() ? rq_frame() : atomic_load(&total_frames); }
uint64_t vk_frame_counter(void) { return renderer_exec_frame(); }  // (the translation's: g_frame_counter == total_frames)
static float g_shot_crop[SF2C_DEVICE_N_4_4B22] = {0, 0, 1, 1};
static double last_fps_time;

const float *vk_shot_crop(void) { return g_shot_crop; }

void backend_present(NvnTexture *t);
void *backend_pool_push(void) { return NULL; }
void backend_pool_pop(void *p) { (void)p; }
void backend_represent(void) {
    NvnTexture *last = atomic_load(&g_last_presented);
    if (!last) { SDL_Delay(SF2C_DEVICE_N_16_B17E); return; }
    uint64_t frame = atomic_load(&total_frames);
    if (rq_on()) rq_present(last, frame, false);
    else backend_present_now(last, frame, false);
}

// (SF2RECOMP_VK_PROF) the present's time by part, per frame in the fps line ("present/frame"): submit = the frame's
// last work submitted, acquire = frame slot + window image (waits included), scale = output scale / widescreen /
// dynamic resolution, blit = the game image into the window (FSR / blit / AA recording), overlay = menu_render (ImGui),
// end = the window image's submission + vkQueuePresentKHR, rest = test hooks and the fps line
enum { PP_SUBMIT, PP_ACQUIRE, PP_SCALE, PP_BLIT, PP_OVERLAY, PP_END, PP_REST, PP_N };
static int pp_on = -1;
static uint64_t pp_ns[PP_N], pp_frames, pp_t;
static void pp_mark(int part) {
    if (!pp_on) return;
    uint64_t t = SDL_GetTicksNS();
    pp_ns[part] += t - pp_t;
    pp_t = t;
}
static const char *pp_text(void) {
    static char s[SF2C_DEVICE_N_192_EB3B];
    s[0] = 0;
    if (!pp_on || !pp_frames) return s;
    double f = (double)pp_frames * SF2C_DEVICE_N_1E6_8C96;
    snprintf(s, sizeof s, SF2C_DEVICE_S_PRESENT_FRAME_SUBMIT_3F_ACQU_616E,
             pp_ns[PP_SUBMIT] / f, pp_ns[PP_ACQUIRE] / f, pp_ns[PP_SCALE] / f, pp_ns[PP_BLIT] / f, pp_ns[PP_OVERLAY] / f,
             pp_ns[PP_END] / f, pp_ns[PP_REST] / f);
    memset(pp_ns, 0, sizeof pp_ns);
    pp_frames = 0;
    return s;
}

// The GPU side of a present (the render thread when it's on, rqueue.c): the present pass, the overlay, shots.
// frame: the game frame when queued; advance is false for repeats and startup movie-only presents.
void backend_present_now(NvnTexture *t, uint64_t frame, bool advance) {
    if (pp_on < 0) pp_on = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_PROF_CC43) != NULL;
    if (pp_on) pp_t = SDL_GetTicksNS();
    TexBackend *b = t ? vk_tex(t, true) : NULL;
    if (b) b->presented = true;
    TexBackend *movie_b = NULL;
    bool first_movie_frame = false;
    if (movie_overlay_active()) {
        first_movie_frame = movie_render_tick();
        NvnTexture *movie = renderer_movie_texture();
        movie_b = vk_tex(movie, true);  // consume the latest NV12 frame before submitting its conversion
    }
    vk_end_pass();
    // the frame's work goes to the GPU now, in a submission of its own: it starts while we wait for the window's image.
    // Its GPU time is counted, and the window image's submission's from after its wait (exec.c gpu_timer_add)
    renderer_submit();
    pp_mark(PP_SUBMIT);
    static const float black[SF2C_DEVICE_N_4_4B22] = {0, 0, 0, 1};
    uint64_t t0 = SDL_GetTicksNS();
    bool have = vk_frame_begin(black);
    pp_mark(PP_ACQUIRE);
    uint64_t nd = SDL_GetTicksNS() - t0;
    if (nd > SF2C_DEVICE_N_30000000_07BA) {
        static int warned;
        if (warned++ < SF2C_DEVICE_N_20_F5CA) LOG(SF2C_DEVICE_S_VK_ACQUIRE_BLOCKED_LLUMS_N_A783, (unsigned long long)(nd / SF2C_DEVICE_N_1000000_6CCE));
    }
    if (have) {
        double dw = g_vk.sc_extent.width, dh = g_vk.sc_extent.height;
        // render scale + placement in the window: runtime/nvn/output_scale.c (Video tab "Internal resolution");
        // GPU time since the last frame (exec.c's GPU timer; -1 without one: dynamic resolution goes by frame times):
        // the game's submissions and the window image's (SF2RECOMP_DYNRES_COUNT_PRESENT=0: the game's only, as before)
        static int count_present = -1;
        if (count_present < 0) count_present = !getenv(SF2C_DEVICE_S_SF2RECOMP_DYNRES_COUNT_PRESEN_D71E) || atoi(getenv(SF2C_DEVICE_S_SF2RECOMP_DYNRES_COUNT_PRESEN_D71E));
        static uint64_t sp_prev[SF2C_DEVICE_N_4_4B22], wt_prev[SF2C_DEVICE_N_4_4B22], cw_prev;
        static bool sp_have;
        uint64_t sp[SF2C_DEVICE_N_4_4B22], wt[SF2C_DEVICE_N_4_4B22];
        for (int i = 0; i < SF2C_DEVICE_N_4_4B22; i++) wt[i] = __atomic_load_n(&g_vk_frame_waits[i], __ATOMIC_RELAXED);  // ([2], [3]: the recorder's)
        double game_ms = -1, win_ms = -1;
        if (vk_gpu_split_ns(sp)) {
            uint64_t cw = vk_cum_wait_ns();
            if (sp_have) {
                double game = (sp[0] - sp_prev[0]) / SF2C_DEVICE_N_1E6_8C96, win = (sp[1] - sp_prev[1]) / SF2C_DEVICE_N_1E6_8C96;
                game_ms = game, win_ms = count_present ? win : -1;
                double slotw = (wt[0] - wt_prev[0]) / SF2C_DEVICE_N_1E6_8C96;
                DynSplit ds = {game, win, (sp[SF2C_DEVICE_N_2_D473] - sp_prev[SF2C_DEVICE_N_2_D473]) / SF2C_DEVICE_N_1E6_8C96, (double)(sp[SF2C_DEVICE_N_3_4E07] - sp_prev[SF2C_DEVICE_N_3_4E07]), slotw,
                               (wt[1] - wt_prev[1]) / SF2C_DEVICE_N_1E6_8C96, (wt[SF2C_DEVICE_N_2_D473] - wt_prev[SF2C_DEVICE_N_2_D473]) / SF2C_DEVICE_N_1E6_8C96,
                               fmax(0, (cw - cw_prev) / SF2C_DEVICE_N_1E6_8C96 - slotw), count_present != 0};
                output_scale_note_split(&ds);
            }
            memcpy(sp_prev, sp, sizeof sp);
            memcpy(wt_prev, wt, sizeof wt_prev);
            cw_prev = cw;
            sp_have = true;
        }
        OutScale os;
        output_scale(dw, dh, false, SDL_GetTicksNS() / SF2C_DEVICE_N_1E9_EFAC, game_ms, win_ms, &os);
        double sx = os.sx, sy = os.sy, vx = os.vx, vy = os.vy, vw = os.vw, vh = os.vh;
        output_scale_log((unsigned long long)frame, dw, dh, &os);  // (diagnostics: [scale] on a change)
        extern void renderer_set_alloc_scale(float ax, float ay);
        renderer_set_alloc_scale((float)os.ax, (float)os.ay);
        renderer_set_scale((float)sx, (float)sy);
        // Report actual source allocation as well as the requested scale, so
        // resizing tests can distinguish target reallocation from a final blit.
        if (b) {
            static uint32_t previous[SF2C_DEVICE_N_4_4B22];
            uint32_t dimensions[SF2C_DEVICE_N_4_4B22] = {b->width, b->height, (uint32_t)dw, (uint32_t)dh};
            if (memcmp(previous, dimensions, sizeof previous)) {
                LOG(SF2C_DEVICE_S_OUTPUT_RENDER_UX_U_WINDOW_UX_4A74,
                    b->width, b->height, (uint32_t)dw, (uint32_t)dh,
                    t ? t->width : 0, t ? t->height : 0, b->scalable,
                    t ? t->samples : 0, t ? t->flags : 0);
                memcpy(previous, dimensions, sizeof previous);
            }
        }
        pp_mark(PP_SCALE);
        static int nlog_pres;
        if (nlog_pres++ < SF2C_DEVICE_N_5_EF2D) LOG(SF2C_DEVICE_S_VK_PRESENT_NOW_FRAME_LLU_T_P_14DE,
            (unsigned long long)frame, t, b, have, b ? b->image : NULL, t ? t->width : 0, t ? t->height : 0,
            g_vk.sc_extent.width, g_vk.sc_extent.height, (float)vx, (float)vy, (float)vw, (float)vh);
        VkCommandBuffer cb = vk_cmd();
        float crop[SF2C_DEVICE_N_4_4B22] = {0, 0, 1, 1};
        float pv[SF2C_DEVICE_N_8_2C62] = {renderer_origin_lower_left() ? SF2C_DEVICE_N_1_0F_CA71 : SF2C_DEVICE_N_0_0F_6895, 0, 0, 0, 0, 0, 1, 1};
        memcpy(g_shot_crop, crop, sizeof crop);
        if (!movie_b) vk_present_draw(cb, b, (float)vx, (float)vy, (float)vw, (float)vh, pv);
        if (movie_b) {
            // Raw NV12 is top-down. Fit the entire video over black, preserving
            // its own aspect ratio independently of the game/window dimensions.
            const float movie_pv[SF2C_DEVICE_N_8_2C62] = {0, 0, 0, 0, 0, 0, 1, 1};
            OutputRect movie_rect;
            output_fit(dw, dh, movie_b->width, movie_b->height, &movie_rect);
            vk_present_draw(cb, movie_b, movie_rect.x, movie_rect.y, movie_rect.w, movie_rect.h, movie_pv);
        }
        pp_mark(PP_BLIT);
        static long menu_at = -1;
        if (menu_at < 0) menu_at = getenv(SF2C_DEVICE_S_SF2RECOMP_MENU_AT_945C) ? atol(getenv(SF2C_DEVICE_S_SF2RECOMP_MENU_AT_945C)) : 0;
        if (advance && menu_at && (long)frame + 1 == menu_at) { extern void menu_test_open(void); menu_test_open(); }
        // (ImGui records through vk_overlay_begin / vk_overlay_end, when it draws anything)
        menu_render((void *)cb, g_vk.sc_extent.width, g_vk.sc_extent.height);
        pp_mark(PP_OVERLAY);
        static long win_shot = -1;
        if (win_shot < 0) win_shot = getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_SHOT_AF3A) ? atol(getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_SHOT_AF3A)) : 0;
        bool shot = advance && win_shot && (long)frame + 1 == win_shot;
        vk_frame_end(shot ? SF2C_DEVICE_S_BUILD_WINDOW_PNG_58F7 : NULL);
        if (movie_guest_ready()) vk_guest_heap_preload();
        if (first_movie_frame) {
            // Let the recorder hand frame zero to the swapchain before starting its audio clock.
            rec_wait_presents(rec_presents_queued());
            movie_presented();
        }
        if (shot && getenv(SF2C_DEVICE_S_SF2RECOMP_EXIT_AFTER_SHOT_7B52)) exit_after_shot();
    } else {
        renderer_submit();
    }
    pp_mark(PP_END);
    pp_frames++;
    extern void vk_prof_frame(uint64_t frame, uint64_t present_ns);
    vk_prof_frame(frame, SDL_GetTicksNS() - t0);
    frames++;
    static long shot_every = -1;
    if (shot_every < 0) { const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_SCREENSHOT_EVERY_CEF8); shot_every = e ? atol(e) : 0; }
    {   // SF2RECOMP_GPUPROF_AT=<frame>[,<frame>...]: GPU time per pass of those frames ("[gpuprof]" in the log)
        extern bool g_gpuprof_frame;
        extern void vk_gpuprof_report(void);
        if (g_gpuprof_frame) { g_gpuprof_frame = false; vk_gpuprof_report(); }
        const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_GPUPROF_AT_4834);
        for (const char *p = e; p && *p;) {
            long at = strtol(p, (char **)&p, SF2C_DEVICE_N_10_4A44);
            if (at == (long)frame) { g_gpuprof_frame = true; break; }
            if (*p == SF2C_DEVICE_CH_EMPTY_EE1B) p++; else break;
        }
    }
    if (advance && g_debug_frame) LOG(SF2C_DEVICE_S_DEBUG_FRAME_LLU_N_3556, (unsigned long long)frame);
    if (advance) {   // SF2RECOMP_SHOT_AT=<frame>,<frame>,... (ascending): game frames -> build/frames/frame_N.png
        static const char *next;
        static int init;
        if (!init) { init = 1; next = getenv(SF2C_DEVICE_S_SF2RECOMP_SHOT_AT_8756); if (next) { mkdir(SF2C_DEVICE_S_BUILD_E99A, SF2C_DEVICE_N_0755_64A5); mkdir(SF2C_DEVICE_S_BUILD_FRAMES_A0DA, SF2C_DEVICE_N_0755_64A5); } }
        while (next && *next) {
            long at = strtol(next, (char **)&next, SF2C_DEVICE_N_10_4A44);
            if ((long)frame < at) { while (next > getenv(SF2C_DEVICE_S_SF2RECOMP_SHOT_AT_8756) && next[-1] != SF2C_DEVICE_CH_EMPTY_EE1B) next--; break; }
            if ((long)frame == at) {
                char path[SF2C_DEVICE_N_256_51E8];
                snprintf(path, sizeof path, SF2C_DEVICE_S_BUILD_FRAMES_FRAME_05LLU_PNG_C878, (unsigned long long)frame);
                renderer_save_texture_tex(b, path);
            }
            if (*next == SF2C_DEVICE_CH_EMPTY_EE1B) next++;
        }
    }
    static long shot_from = -1;
    if (shot_from < 0) { const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_SCREENSHOT_FROM_32B7); shot_from = e ? atol(e) : 0; }
    if (advance && shot_every && frame % shot_every == 0 && (long)frame >= shot_from) {
        char path[SF2C_DEVICE_N_256_51E8];
        mkdir(SF2C_DEVICE_S_BUILD_E99A, SF2C_DEVICE_N_0755_64A5);
        if (shot_from) mkdir(SF2C_DEVICE_S_BUILD_FRAMES_A0DA, SF2C_DEVICE_N_0755_64A5);
        snprintf(path, sizeof path, shot_from ? SF2C_DEVICE_S_BUILD_FRAMES_FRAME_05LLU_PNG_C878 : SF2C_DEVICE_S_BUILD_FRAME_05LLU_PNG_CBB4,
                 (unsigned long long)frame);
        renderer_save_texture_tex(b, path);
        // a series (SCREENSHOT_FROM) runs on until SF2RECOMP_WINDOW_SHOT exits
        if (getenv(SF2C_DEVICE_S_SF2RECOMP_EXIT_AFTER_SHOT_7B52) && !shot_from) exit_after_shot();
    }
    double now = SDL_GetTicks() / SF2C_DEVICE_N_1000_0_4250;
    static double fps_iv = -1, prev_present, max_gap;
    if (fps_iv < 0) fps_iv = getenv(SF2C_DEVICE_S_SF2RECOMP_FPS_INTERVAL_D507) ? atof(getenv(SF2C_DEVICE_S_SF2RECOMP_FPS_INTERVAL_D507)) : SF2C_DEVICE_N_2_0_D84B;
    if (prev_present > 0 && now - prev_present > max_gap) max_gap = now - prev_present;
    static double hitch_ms = -1;  // SF2RECOMP_LOG_HITCH_MS=<ms>: every present gap over that (tests: hitches per run)
    if (hitch_ms < 0) hitch_ms = getenv(SF2C_DEVICE_S_SF2RECOMP_LOG_HITCH_MS_631F) ? atof(getenv(SF2C_DEVICE_S_SF2RECOMP_LOG_HITCH_MS_631F)) : 0;
    if (hitch_ms > 0 && prev_present > 0 && (now - prev_present) * SF2C_DEVICE_N_1000_4051 > hitch_ms)
        LOG(SF2C_DEVICE_S_HITCH_T_2F_FRAME_LLU_GAP_0F__3F95, now, (unsigned long long)frame, (now - prev_present) * SF2C_DEVICE_N_1000_4051);
    prev_present = now;
    pp_mark(PP_REST);  // (the fps line itself is not counted)
    if (now - last_fps_time >= fps_iv) {
        extern const char *game_level_name(void);
        if (last_fps_time > 0)
            LOG(SF2C_DEVICE_S_VK_T_1F_FRAME_LLU_1F_FPS_MAX_EF24, now, (unsigned long long)frame,
                frames / (now - last_fps_time), max_gap * SF2C_DEVICE_N_1000_0_4250, game_level_name(), renderer_stats(), pp_text());
        max_gap = 0;
        frames = 0;
        last_fps_time = now;
    }
    static double mem_at = SF2C_DEVICE_N_60_0_DB58;  // (memory in steady state: a minute in, then every 10 minutes)
    if (now >= mem_at) { vk_log_memory(SF2C_DEVICE_S_PLAYING_D83B); mem_at = now + SF2C_DEVICE_N_600_0_F7EA; }
}

// The game thread's side of a present: the frame counters and the per-frame hooks (input, pacing, test hooks).
static void present_hooks(void) {
    uint64_t frame = atomic_fetch_add(&total_frames, 1) + 1;
    g_frame_counter++;
    extern void input_frame(uint64_t frame);
    input_frame(frame);
    static long slow_ms = -1;  // SF2RECOMP_SIM_SLOW_FRAME_MS=<ms>: sleep per presented frame (simulate a slow machine)
    if (slow_ms < 0) slow_ms = getenv(SF2C_DEVICE_S_SF2RECOMP_SIM_SLOW_FRAME_MS_ECED) ? atol(getenv(SF2C_DEVICE_S_SF2RECOMP_SIM_SLOW_FRAME_MS_ECED)) : 0;
    static long slow_until = -1;  // SF2RECOMP_SLOW_FRAME_UNTIL=<frame>: the stall ends there (frame pacing recovery tests)
    if (slow_until < 0) slow_until = getenv(SF2C_DEVICE_S_SF2RECOMP_SLOW_FRAME_UNTIL_0859) ? atol(getenv(SF2C_DEVICE_S_SF2RECOMP_SLOW_FRAME_UNTIL_0859)) : 0;
    if (slow_ms > 0 && frame > SF2C_DEVICE_N_2600_3A0E && (!slow_until || (long)frame < slow_until)) SDL_Delay((Uint32)slow_ms);
    extern void fnprof_frame(uint64_t frame);
    fnprof_frame(frame);
    static long debug_at = -1;
    if (debug_at < 0) { const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_DEBUG_FRAME_DEA8); debug_at = e ? atol(e) : 0; }
    extern volatile int g_debug_frame_request;
    if (g_debug_frame_request) { g_debug_frame_request = 0; debug_at = (long)frame + 1; }
    // SF2RECOMP_DEBUG_FRAMES=<n> (with SF2RECOMP_DEBUG_FRAME / F9): dump n consecutive frames (build/pass_<frame>_NNN.png)
    static long debug_n = -1;
    if (debug_n < 0) { const char *e = getenv(SF2C_DEVICE_S_SF2RECOMP_DEBUG_FRAMES_8FB4); debug_n = e && atol(e) > 1 ? atol(e) : 1; }
    g_debug_frame = debug_at && frame >= (uint64_t)debug_at && frame < (uint64_t)debug_at + (uint64_t)debug_n;
}

void backend_present(NvnTexture *t) {
    atomic_store(&g_last_presented, t);
    atomic_store(&last_game_present_ns, SDL_GetTicksNS());
    uint64_t frame = atomic_load(&total_frames), t0 = plat_now_ns();
    if (rq_on()) rq_present(t, frame, true);
    else backend_present_now(t, frame, true);
    uint64_t t1 = plat_now_ns();
    g_present_ns += t1 - t0;  // (SF2RECOMP_LOG_PACE, pace.c)
    present_hooks();
    g_hooks_ns += plat_now_ns() - t1;
}

// The guest runs on its own thread. Keep the intro visible while that thread loads game assets
// and has not issued a game present yet (or is temporarily stalled between presents).
static void backend_movie_present(bool dismissed) {
    static bool was_active, reveal_pending;
    bool active = movie_overlay_active();
    if ((was_active && !active) || dismissed) reveal_pending = true;
    was_active = active;
    // With the render queue disabled, backend calls belong to the guest's graphics thread.
    // Its normal presents still show the movie and keep the frame-zero audio gate intact.
    if (!rq_on()) return;
    uint64_t frame = atomic_load(&total_frames);
    if (!active) {
        // Loading can leave the guest several seconds between presents. Replace
        // the last movie frame now, retrying if a present is already queued.
        if (reveal_pending && rq_try_present(atomic_load(&g_last_presented), frame)) reveal_pending = false;
        return;
    }
    reveal_pending = false;
    uint64_t now = SDL_GetTicksNS();
    uint64_t last = atomic_load(&last_game_present_ns);
    if (last && now - last < SF2C_DEVICE_N_50000000ULL_F253) return;
    (void)rq_try_present(NULL, frame);
}

// ================================================================== UI thread
void vk_exec_init(void);
void vk_shaders_init(void);

// The end of a validation run (SF2RECOMP_VK_VALIDATION, after backend_quit: the GPU idle, the pipeline cache kept):
// the debug messenger goes, then the process ends at once - no vkDestroyDevice / vkDestroyInstance, no atexit
// handlers, no DLL teardown. _exit() alone isn't enough on Windows: it ends in ExitProcess, which runs the layer's
// and the driver's DLL teardown after every other thread is gone, and with the Khronos layer on NVIDIA that left the
// process hung there and unkillable (one thread waiting, gigabytes of the layer's state).
static _Noreturn void validation_exit(void) {
    if (messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT f =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_vk.inst, SF2C_DEVICE_S_VKDESTROYDEBUGUTILSMESSENGER_2A60);
        VkDebugUtilsMessengerEXT m = messenger;
        messenger = VK_NULL_HANDLE;
        if (f) f(g_vk.inst, m, NULL);
    }
    LOG(SF2C_DEVICE_S_VK_VALIDATION_RUN_ENDING_WIT_689D);
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32) || defined(__linux__)
    plat_terminate();
#endif
    _Exit(0);
}

// SF2RECOMP_EXIT_AFTER_SHOT: the test run ends with its shot. As it always did (_exit: no cache save), except under
// validation: backend_quit, then validation_exit.
static void exit_after_shot(void) {
    fflush(stderr);
    if (validating) {
        backend_quit();
        validation_exit();
    }
    _exit(0);
}

// Quitting, before exit() (the window closed; also from exit() itself, quick_exit_handler): the render thread stops
// between items, so no image is acquired or presented any more; the recorder thread stops making driver calls (the
// packets it still had are dropped, record.c rec_quit) and is idle; then the GPU finishes and the pipeline cache is
// kept. All of it before exit() runs the atexit handlers and the libraries' destructors: a Vulkan layer loaded with
// the instance (the Steam overlay on the Steam Deck, #40) tears its swapchain state down there, and a present from
// the render thread after that crashed in vkQueuePresentKHR. The swapchain, surface and window are never destroyed
// explicitly: the process ends with them.
void backend_quit(void) {
    static atomic_flag once = ATOMIC_FLAG_INIT;
    if (atomic_flag_test_and_set(&once)) return;
    static int trace = -1;
    if (trace < 0) trace = getenv(SF2C_DEVICE_S_SF2RECOMP_VK_TRACE_B60E) != NULL;
    if (trace) LOG(SF2C_DEVICE_S_VK_QUIT_STOPPING_THE_RENDER__1C80);
    rq_stop();
    if (trace) LOG(SF2C_DEVICE_S_VK_QUIT_STOPPING_THE_RECORDE_7513);
    rec_quit();  // (what it still had is dropped, not run: no submission or present after this; the queue idle for the wait)
    if (!g_vk.dev) return;
    vk_guest_heap_preload_end();
    if (trace) LOG(SF2C_DEVICE_S_VK_QUIT_WAITING_FOR_THE_GPU__0F9C);
    vkDeviceWaitIdle(g_vk.dev);
    if (trace) LOG(SF2C_DEVICE_S_VK_QUIT_SAVING_THE_PIPELINE__4EB6);
    vk_pipeline_cache_save();
}

// exit() from any thread: backend_quit (when the window close didn't already), then end the process without the DLL
// teardown (driver / validation-layer threads can stall it for minutes while the game's threads still run).
static void quick_exit_handler(void) {
    backend_quit();
    if (validating) validation_exit();
    fflush(stdout);
    fflush(stderr);
#if defined(_WIN32) || defined(__linux__)
    plat_terminate();
#endif
}

void ui_window_init(void) {
    if (window) return;
    atexit(quick_exit_handler);
    settings_load();
    if (!SDL_SetAppMetadata(SF2_APP_NAME, game_display_version()[0] ? game_display_version() : NULL, SF2_APP_ID))
        LOG(SF2C_DEVICE_S_DESKTOP_SDL_APP_METADATA_S_N_1379, SDL_GetError());
    input_apply_background();  // controllers in the background: "Allow background inputs" (Controls tab)
#ifdef __ANDROID__
    // SDL's resizable window otherwise overrides the Activity's landscape
    // request and permits portrait on phones.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, SF2C_DEVICE_S_LANDSCAPELEFT_LANDSCAPERIGHT_F992);
    // Unity receives real contacts. A synthesized mouse would duplicate taps
    // and collapse simultaneous joystick/action-button fingers into one pointer.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, SF2C_DEVICE_S_0_9808);
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, SF2C_DEVICE_S_0_9808);
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, SF2C_DEVICE_S_1_3915);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) FATAL(SF2C_DEVICE_S_SDL_INIT_S_N_7134, SDL_GetError());
    movie_preload_intro();
    input_load_user_mappings();
    int win_w = SF2C_DEVICE_N_1280_6F13, win_h = SF2C_DEVICE_N_720_D829;
    if (getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_7651)) sscanf(getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_7651), SF2C_DEVICE_S_DX_D_3DD6, &win_w, &win_h);
    SDL_WindowFlags wflags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (getenv(SF2C_DEVICE_S_SF2RECOMP_HIDDEN_AFF5)) wflags |= SDL_WINDOW_HIDDEN;
    {   // scripted test runs (user input ignored: runtime/hle/input.c input_user_blocked) open without taking the
        // keyboard focus from whatever the player is doing
        const char *v = getenv(SF2C_DEVICE_S_SF2RECOMP_NO_USER_INPUT_EB20);
        bool scripted = v ? atoi(v) != 0 : (getenv(SF2C_DEVICE_S_SF2RECOMP_INPUT_D0A3) || getenv(SF2C_DEVICE_S_SF2RECOMP_HOST_INPUT_8D91) || getenv(SF2C_DEVICE_S_SF2RECOMP_BIND_TEST_C2F2));
        if (scripted) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, SF2C_DEVICE_S_0_9808);
    }
    window = SDL_CreateWindow(game_window_title(), win_w, win_h, wflags);
    if (!window) FATAL(SF2C_DEVICE_S_SDL_CREATEWINDOW_S_N_9FD0, SDL_GetError());
    SDL_DisableScreenSaver();
    if (g_settings.aspect==ASPECT_NATIVE) {
        int width=0,height=0;
        if (SDL_GetWindowSize(window,&width,&height) && width>0 && height>0) {
            g_settings.native_width=width;g_settings.native_height=height;
            LOG(SF2C_DEVICE_S_VIDEO_NATIVE_ASPECT_WINDOW_D_EE1F,width,height,settings_screen_width(SF2C_DEVICE_N_1080_32EB));
        }
    }
    if (game_icon_path()[0]) {
        SDL_Surface *icon = SDL_LoadPNG(game_icon_path());
        if (!icon) LOG(SF2C_DEVICE_S_DESKTOP_CANNOT_LOAD_ORIGINAL_E459, SDL_GetError());
        else {
            if (!SDL_SetWindowIcon(window, icon)) LOG(SF2C_DEVICE_S_DESKTOP_WINDOW_ICON_S_N_2B41, SDL_GetError());
            else LOG(SF2C_DEVICE_S_DESKTOP_ORIGINAL_GAME_ICON_L_35D0);
            SDL_DestroySurface(icon);
        }
    }
    // the Fullscreen setting applies from the start screen on (not only once the game runs); test windows stay windows
    if (g_settings.fullscreen && !getenv(SF2C_DEVICE_S_SF2RECOMP_WINDOW_7651) && !getenv(SF2C_DEVICE_S_SF2RECOMP_HIDDEN_AFF5)) SDL_SetWindowFullscreen(window, true);
    create_instance();
    if (!SDL_Vulkan_CreateSurface(window, g_vk.inst, NULL, &g_vk.surface)) FATAL(SF2C_DEVICE_S_SDL_VULKAN_CREATESURFACE_S_N_5582, SDL_GetError());
    pick_device();
    create_device();
    create_swapchain();
    vk_exec_init();
    vk_shaders_init();
}

SDL_Window *ui_window(void) { return window; }
bool ui_window_focused(void) { return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS); }  // (any thread, for a log)

void backend_init(void) {
    while (!atomic_load(&ui_ready)) SDL_Delay(1);
    LOG(SF2C_DEVICE_S_VK_S_SWAPCHAIN_UX_U_N_091A, g_vk.props.deviceName, g_vk.sc_extent.width, g_vk.sc_extent.height);
}

// ---- in-place reboot (runtime/main.c teardown; main thread, guest threads stopped)
void backend_reset(void) {
    if (!g_vk.dev) return;
    rq_drain();  // (the render thread: everything queued ran; it idles while the game is down)
    rec_drain();  // (the recorder: everything the render thread queued is recorded and submitted)
    vkDeviceWaitIdle(g_vk.dev);
    extern void vk_textures_reset(void);
    extern void vk_exec_reset(void);
    vk_textures_reset();
    vk_exec_reset();
    atomic_store(&g_last_presented, NULL);
    atomic_store(&last_game_present_ns, 0);
}
void backend_guest_memory_reset(void) {}  // (exec.c re-imports the heap on first use)
void backend_clear_window(void) {
    if (rq_stopping()) return;
    rq_drain();
    static const float black[SF2C_DEVICE_N_4_4B22] = {0, 0, 0, 1};
    if (g_vk.dev && vk_frame_begin(black)) vk_frame_end(NULL);
}

static _Noreturn void ui_quit(void) {
    input_rumble_stop();
    LOG(SF2C_DEVICE_S_DESKTOP_EXIT_REQUESTED_N_1396);
    backend_quit();
    exit(0);
}

int ui_run(void) {
    ui_window_init();
    atomic_store(&ui_ready, true);
    for (;;) {
        SDL_Event e;
        while (SDL_WaitEventTimeout(&e, SF2C_DEVICE_N_16_B17E)) {
            input_rumble_poll();
            if (e.type == SDL_EVENT_QUIT || menu_quit_requested()) ui_quit();
            extern bool input_user_blocked(const SDL_Event *e);
            if (input_user_blocked(&e)) continue;  // scripted test runs: the real keyboard / mouse / pads do nothing (as metal.m)
#ifdef __ANDROID__
            // Android Back follows the existing Escape policy: back in menus,
            // pause during combat. Process it before native modal UI capture.
            if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.scancode == SDL_SCANCODE_AC_BACK) {
                e.key.scancode = SDL_SCANCODE_ESCAPE;
                e.key.key = SDLK_ESCAPE;
            }
#endif
            input_cursor_event(&e);
            if (menu_handle_event(&e)) continue;
            input_handle_event(&e);
        }
        if (menu_quit_requested()) ui_quit();
        menu_apply_window_requests(window);
        input_rumble_poll();
        backend_movie_present(movie_poll());
    }
}
