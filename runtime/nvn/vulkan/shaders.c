#include "../../constants/nvn_vulkan.h"
// GLSL -> SPIR-V (shaderc), descriptor layouts from the SPIR-V itself, translated NVN programs, built-in shaders.
#include <pthread.h>
#include <shaderc/shaderc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shaders.h"
#include "../screen_filter.h"

void vk_shaders_init(void) {  // (compilers are per thread: check shaderc works at all)
    shaderc_compiler_t c = shaderc_compiler_initialize();
    if (!c) FATAL(SF2C_SHADERS_S_SHADERC_COMPILER_INITIALIZE__41BC);
    shaderc_compiler_release(c);
}

static shaderc_shader_kind kind_of(int stage) {
    return stage == STAGE_VERTEX ? shaderc_vertex_shader : stage == STAGE_FRAGMENT ? shaderc_fragment_shader : shaderc_compute_shader;
}

// SPIR-V disk cache: cache/spirv/<hash of stage + source>.spv, so a known shader costs a file read instead of a
// GLSL compile (the driver's pipeline cache only covers the step after this one).
static uint64_t src_hash(const char *src, int stage) {
    uint64_t h = (SF2C_SHADERS_N_0XCBF29CE484222325ULL_7EE8 ^ (uint64_t)stage) + SF2C_SHADERS_N_3_4E07;  // + version of the post-processing (no_contraction)
    for (const char *p = src; *p; p++) { h ^= (uint8_t)*p; h *= SF2C_SHADERS_N_0X100000001B3ULL_3E10; }
    return h;
}

static uint32_t *cache_load(uint64_t h, size_t *nwords) {
    char path[SF2C_SHADERS_N_64_A68B];
    snprintf(path, sizeof path, SF2C_SHADERS_S_CACHE_SPIRV_016LLX_SPV_9CA7, (unsigned long long)h);
    FILE *f = fopen(path, SF2C_SHADERS_S_RB_46CA);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *d = n > SF2C_SHADERS_N_20_F5CA && n % SF2C_SHADERS_N_4_4B22 == 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    if (d && d[0] != SF2C_SHADERS_N_0X07230203_0BEA) { free(d); d = NULL; }
    if (d) *nwords = (size_t)n / SF2C_SHADERS_N_4_4B22;
    return d;
}

static void cache_store(uint64_t h, const uint32_t *spv, size_t nwords) {
    static int made;
    if (!made) { made = 1; mkdir(SF2C_SHADERS_S_CACHE_C87A, SF2C_SHADERS_N_0755_64A5); mkdir(SF2C_SHADERS_S_CACHE_SPIRV_DA88, SF2C_SHADERS_N_0755_64A5); }
    char path[SF2C_SHADERS_N_64_A68B], tmp[SF2C_SHADERS_N_80_4844];
    snprintf(path, sizeof path, SF2C_SHADERS_S_CACHE_SPIRV_016LLX_SPV_9CA7, (unsigned long long)h);
    snprintf(tmp, sizeof tmp, SF2C_SHADERS_S_S_LLX_EE67, path, (unsigned long long)(uintptr_t)&made ^ (unsigned long long)(uintptr_t)spv);
    FILE *f = fopen(tmp, SF2C_SHADERS_S_WB_3D6A);
    if (!f) return;
    bool ok = fwrite(spv, SF2C_SHADERS_N_4_4B22, nwords, f) == nwords;
    fclose(f);
    if (!ok || rename(tmp, path) != 0) remove(tmp);  // (another thread may have stored it first)
}

// Vertex shaders: every float add/sub/mul/div/fma gets NoContraction, so no compiler fuses or splits them. Depth
// pre-pass and colour pass are different shaders computing the same position, and the colour pass tests EQUAL:
// any difference in fusing leaves holes (foliage flickered black). GLSL's `precise gl_Position` only covers what
// glslang traces back from the position, which the translated register code defeats.
static bool float_arith(const uint32_t *w, uint32_t wc, uint32_t op) {
    switch (op) {
    case SF2C_SHADERS_N_127_922C: case SF2C_SHADERS_N_129_6566: case SF2C_SHADERS_N_131_EECA: case SF2C_SHADERS_N_133_D2F4: case SF2C_SHADERS_N_136_36EB:  // FNegate FAdd FSub FMul FDiv
    case SF2C_SHADERS_N_142_D4EE: case SF2C_SHADERS_N_143_D6F0: case SF2C_SHADERS_N_144_5EC1: case SF2C_SHADERS_N_145_BE47: case SF2C_SHADERS_N_146_0A5B: case SF2C_SHADERS_N_148_EC2E:  // vector/matrix products, Dot
        return true;
    case SF2C_SHADERS_N_12_6B51: return wc >= SF2C_SHADERS_N_5_EF2D && w[SF2C_SHADERS_N_4_4B22] == SF2C_SHADERS_N_50_1A65;  // ExtInst GLSL.std.450 Fma
    default: return false;
    }
}

static uint32_t *no_contraction(uint32_t *spv, size_t *nwords) {
    enum { OP_DECORATE = SF2C_SHADERS_N_71_7F22, DEC_NO_CONTRACTION = SF2C_SHADERS_N_42_7347 };
    size_t n = *nwords, insert = 0, count = 0;
    uint32_t bound = spv[SF2C_SHADERS_N_3_4E07];
    uint8_t *done = calloc(bound, 1);  // already decorated (glslang does it for what it traces from precise)
    for (size_t i = SF2C_SHADERS_N_5_EF2D; i < n;) {
        uint32_t wc = spv[i] >> SF2C_SHADERS_N_16_B17E, op = spv[i] & SF2C_SHADERS_N_0XFFFF_F51F;
        if (!wc) break;
        if (!insert && op >= SF2C_SHADERS_N_19_9400 && op <= SF2C_SHADERS_N_39_0B91) insert = i;  // first type declaration: end of the annotations
        if (op == OP_DECORATE && wc >= SF2C_SHADERS_N_3_4E07 && spv[i + SF2C_SHADERS_N_2_D473] == DEC_NO_CONTRACTION && spv[i + 1] < bound) done[spv[i + 1]] = 1;
        i += wc;
    }
    for (size_t i = SF2C_SHADERS_N_5_EF2D; i < n;) {
        uint32_t wc = spv[i] >> SF2C_SHADERS_N_16_B17E, op = spv[i] & SF2C_SHADERS_N_0XFFFF_F51F;
        if (!wc) break;
        if (float_arith(spv + i, wc, op) && spv[i + SF2C_SHADERS_N_2_D473] < bound && !done[spv[i + SF2C_SHADERS_N_2_D473]]) count++;
        i += wc;
    }
    if (!insert || !count) { free(done); return spv; }
    uint32_t *out = malloc((n + count * SF2C_SHADERS_N_3_4E07) * SF2C_SHADERS_N_4_4B22);
    memcpy(out, spv, insert * SF2C_SHADERS_N_4_4B22);
    size_t o = insert;
    for (size_t i = SF2C_SHADERS_N_5_EF2D; i < n;) {
        uint32_t wc = spv[i] >> SF2C_SHADERS_N_16_B17E, op = spv[i] & SF2C_SHADERS_N_0XFFFF_F51F;
        if (!wc) break;
        if (float_arith(spv + i, wc, op) && spv[i + SF2C_SHADERS_N_2_D473] < bound && !done[spv[i + SF2C_SHADERS_N_2_D473]]) {
            done[spv[i + SF2C_SHADERS_N_2_D473]] = 1;
            out[o++] = SF2C_SHADERS_N_3U_3B80 << SF2C_SHADERS_N_16_B17E | OP_DECORATE;
            out[o++] = spv[i + SF2C_SHADERS_N_2_D473];  // result id
            out[o++] = DEC_NO_CONTRACTION;
        }
        i += wc;
    }
    memcpy(out + o, spv + insert, (n - insert) * SF2C_SHADERS_N_4_4B22);
    *nwords = o + (n - insert);
    free(done);
    free(spv);
    return out;
}

// SPIR-V words of src, or NULL (error logged with the source). *nwords set on success. Thread-safe: one shaderc
// compiler per thread (a shared one behind a lock serialised all the pipeline workers).
// No spirv-opt by default: it rewrote the depth pre-pass and colour-pass vertex shaders differently enough that
// their depths stopped matching (EQUAL test: black holes in foliage), and it was most of the compile time; the
// driver optimises anyway. LARECOMP_VK_OPT=1 turns it back on (experiments).
uint32_t *vk_glsl_to_spirv(const char *src, int stage, const char *name, size_t *nwords) {
    uint64_t h = src_hash(src, stage);
    static int nocache = -1;
    if (nocache < 0) nocache = getenv(SF2C_SHADERS_S_LARECOMP_VK_NO_SPIRV_CACHE_E859) != NULL;
    uint32_t *out = nocache ? NULL : cache_load(h, nwords);
    if (out) return out;
    static __thread shaderc_compiler_t comp;
    if (!comp) comp = shaderc_compiler_initialize();
    shaderc_compile_options_t o = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(o, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shaderc_compile_options_set_optimization_level(o, getenv(SF2C_SHADERS_S_LARECOMP_VK_OPT_0B7E) ? shaderc_optimization_level_performance
                                                                                  : shaderc_optimization_level_zero);
    shaderc_compilation_result_t r = shaderc_compile_into_spv(comp, src, strlen(src), kind_of(stage), name, SF2C_SHADERS_S_MAIN_E4A9, o);
    shaderc_compile_options_release(o);
    if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
        static int nlog;
        if (nlog++ < SF2C_SHADERS_N_20_F5CA) LOG(SF2C_SHADERS_S_SHADER_S_GLSL_COMPILE_FAILED_08F8, name, shaderc_result_get_error_message(r), src);
    } else {
        size_t n = shaderc_result_get_length(r);
        out = malloc(n);
        memcpy(out, shaderc_result_get_bytes(r), n);
        *nwords = n / SF2C_SHADERS_N_4_4B22;
        if (stage == STAGE_VERTEX) out = no_contraction(out, nwords);
        if (!nocache) cache_store(h, out, *nwords);
    }
    shaderc_result_release(r);
    return out;
}

VkShaderModule vk_module(const uint32_t *spv, size_t nwords) {
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = nwords * SF2C_SHADERS_N_4_4B22, .pCode = spv};
    VkShaderModule m;
    return vkCreateShaderModule(g_vk.dev, &ci, NULL, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

VkShaderModule vk_compile_glsl(const char *src, int stage, const char *name) {
    size_t n;
    uint32_t *spv = vk_glsl_to_spirv(src, stage, name, &n);
    if (!spv) return VK_NULL_HANDLE;
    VkShaderModule m = vk_module(spv, n);
    free(spv);
    return m;
}

// ================================================================== reflection
// Just enough SPIR-V parsing to list the descriptor bindings a module declares (set, binding, type).
enum { OP_TYPE_IMAGE = SF2C_SHADERS_N_25_B7A5, OP_TYPE_SAMPLER = SF2C_SHADERS_N_26_5F9C, OP_TYPE_SAMPLED_IMAGE = SF2C_SHADERS_N_27_6706, OP_TYPE_ARRAY = SF2C_SHADERS_N_28_59E1, OP_TYPE_RUNTIME_ARRAY = SF2C_SHADERS_N_29_3513,
       OP_TYPE_STRUCT = SF2C_SHADERS_N_30_624B, OP_TYPE_POINTER = SF2C_SHADERS_N_32_E29C, OP_VARIABLE = SF2C_SHADERS_N_59_3E1E, OP_DECORATE = SF2C_SHADERS_N_71_7F22 };
enum { DEC_BLOCK = SF2C_SHADERS_N_2_D473, DEC_BUFFER_BLOCK = SF2C_SHADERS_N_3_4E07, DEC_BINDING = SF2C_SHADERS_N_33_C6F3, DEC_SET = SF2C_SHADERS_N_34_86E5 };
enum { SC_UNIFORM_CONSTANT = 0, SC_UNIFORM = SF2C_SHADERS_N_2_D473, SC_STORAGE_BUFFER = SF2C_SHADERS_N_12_6B51 };

int vk_reflect(const uint32_t *spv, size_t n, VkBindingInfo *out, int max) {
    if (n < SF2C_SHADERS_N_5_EF2D || spv[0] != SF2C_SHADERS_N_0X07230203_0BEA) return 0;
    uint32_t bound = spv[SF2C_SHADERS_N_3_4E07];
    int32_t *binding = malloc(bound * sizeof *binding), *set = malloc(bound * sizeof *set);
    uint8_t *flags = calloc(bound, 1);    // 1 Block, 2 BufferBlock
    uint32_t *op = calloc(bound, sizeof *op), *ref = calloc(bound, sizeof *ref), *sc = calloc(bound, sizeof *sc);
    for (uint32_t i = 0; i < bound; i++) binding[i] = set[i] = -1;
    for (size_t i = SF2C_SHADERS_N_5_EF2D; i < n;) {
        uint32_t wc = spv[i] >> SF2C_SHADERS_N_16_B17E, o = spv[i] & SF2C_SHADERS_N_0XFFFF_F51F;
        if (!wc) break;
        const uint32_t *w = spv + i;
        if (o == OP_DECORATE && wc >= SF2C_SHADERS_N_3_4E07 && w[1] < bound) {
            if (w[SF2C_SHADERS_N_2_D473] == DEC_BINDING && wc >= SF2C_SHADERS_N_4_4B22) binding[w[1]] = (int32_t)w[SF2C_SHADERS_N_3_4E07];
            else if (w[SF2C_SHADERS_N_2_D473] == DEC_SET && wc >= SF2C_SHADERS_N_4_4B22) set[w[1]] = (int32_t)w[SF2C_SHADERS_N_3_4E07];
            else if (w[SF2C_SHADERS_N_2_D473] == DEC_BLOCK) flags[w[1]] |= 1;
            else if (w[SF2C_SHADERS_N_2_D473] == DEC_BUFFER_BLOCK) flags[w[1]] |= SF2C_SHADERS_N_2_D473;
        } else if ((o == OP_TYPE_IMAGE || o == OP_TYPE_SAMPLER || o == OP_TYPE_SAMPLED_IMAGE || o == OP_TYPE_STRUCT) && wc >= SF2C_SHADERS_N_2_D473 && w[1] < bound) {
            op[w[1]] = o;
        } else if ((o == OP_TYPE_ARRAY || o == OP_TYPE_RUNTIME_ARRAY) && wc >= SF2C_SHADERS_N_3_4E07 && w[1] < bound) {
            op[w[1]] = o;
            ref[w[1]] = w[SF2C_SHADERS_N_2_D473];
        } else if (o == OP_TYPE_POINTER && wc >= SF2C_SHADERS_N_4_4B22 && w[1] < bound) {
            op[w[1]] = o;
            sc[w[1]] = w[SF2C_SHADERS_N_2_D473];
            ref[w[1]] = w[SF2C_SHADERS_N_3_4E07];
        } else if (o == OP_VARIABLE && wc >= SF2C_SHADERS_N_4_4B22 && w[SF2C_SHADERS_N_2_D473] < bound && w[1] < bound) {
            op[w[SF2C_SHADERS_N_2_D473]] = o;
            ref[w[SF2C_SHADERS_N_2_D473]] = w[1];  // pointer type
            sc[w[SF2C_SHADERS_N_2_D473]] = w[SF2C_SHADERS_N_3_4E07];
        }
        i += wc;
    }
    int cnt = 0;
    for (uint32_t id = 0; id < bound && cnt < max; id++) {
        if (op[id] != OP_VARIABLE || binding[id] < 0) continue;
        uint32_t t = ref[ref[id]];  // pointee
        while (t < bound && (op[t] == OP_TYPE_ARRAY || op[t] == OP_TYPE_RUNTIME_ARRAY)) t = ref[t];
        VkDescriptorType dt;
        if (t >= bound) continue;
        if (op[t] == OP_TYPE_SAMPLED_IMAGE) dt = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        else if (op[t] == OP_TYPE_IMAGE) dt = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        else if (op[t] == OP_TYPE_SAMPLER) dt = VK_DESCRIPTOR_TYPE_SAMPLER;
        else if (op[t] == OP_TYPE_STRUCT && (sc[id] == SC_STORAGE_BUFFER || (flags[t] & SF2C_SHADERS_N_2_D473))) dt = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        else if (op[t] == OP_TYPE_STRUCT && sc[id] == SC_UNIFORM) dt = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        else continue;
        out[cnt++] = (VkBindingInfo){(uint32_t)(set[id] < 0 ? 0 : set[id]), (uint32_t)binding[id], dt};
    }
    free(binding); free(set); free(flags); free(op); free(ref); free(sc);
    return cnt;
}

VkDescriptorSetLayout vk_set_layout(const VkBindingInfo *b, int n, uint32_t set, VkShaderStageFlags stages) {
    VkDescriptorSetLayoutBinding lb[SF2C_SHADERS_N_96_7B1A];
    uint32_t k = 0;
    for (int i = 0; i < n && k < SF2C_SHADERS_N_96_7B1A; i++)
        if (b[i].set == set) lb[k++] = (VkDescriptorSetLayoutBinding){b[i].binding, b[i].type, 1, stages, NULL};
    VkDescriptorSetLayoutCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = k, .pBindings = lb};
    VkDescriptorSetLayout l;
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &ci, NULL, &l));
    return l;
}

// ================================================================== built-in shaders
static const char *builtin_src(const char *name, int *stage) {
    if (!strcmp(name, SF2C_SHADERS_S_PRESENT_VERT_0EDD)) {
        *stage = STAGE_VERTEX;
        // pv[0].x: flip (lower-left origin), pv[1]: uv crop (origin, size). Fullscreen triangle; Vulkan NDC y points down.
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_1B63
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC2_U_8DC3
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC2_P_VEC2_GL_VERTEXINDEX_1_EF34
               SF2C_SHADERS_S_GL_POSITION_VEC4_P_2_0_1_0_0_AD79
               SF2C_SHADERS_S_VEC2_U_VEC2_P_X_PC_A_X_0_5_1_2AC7
               SF2C_SHADERS_S_UV_PC_CROP_XY_U_PC_CROP_ZW_N_4C64
               SF2C_SHADERS_S_N_E09D;
    }
    if (!strcmp(name, SF2C_SHADERS_S_PRESENT_FRAG_EEE9)) {
        *stage = STAGE_FRAGMENT;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_F635
               SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_6A3A  // (the screen filter)
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_IN_VEC2_UV_7F0F
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC3_C_TEXTURE_T_UV_RGB_N_038A
               SCREEN_FILTER_SRC(SF2C_SHADERS_S_VEC3_1818)
               SF2C_SHADERS_S_O_VEC4_C_1_0_N_6D4D
               SF2C_SHADERS_S_N_E09D;
    }
    // ---- decoded video, NV12 -> RGBA (exec.c vk_convert_nv12): limited range BT.601, as swscale did (the movies
    // declare no matrix). Fullscreen triangle; Vulkan NDC y points down, so uv = p.
    if (!strcmp(name, SF2C_SHADERS_S_NV12_VERT_BCD1)) {
        *stage = STAGE_VERTEX;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC2_U_8DC3
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC2_P_VEC2_GL_VERTEXINDEX_1_EF34
               SF2C_SHADERS_S_GL_POSITION_VEC4_P_2_0_1_0_0_AD79
               SF2C_SHADERS_S_UV_P_N_A00F
               SF2C_SHADERS_S_N_E09D;
    }
    if (!strcmp(name, SF2C_SHADERS_S_NV12_FRAG_482D)) {
        *stage = STAGE_FRAGMENT;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_0A35
               SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_1_UNIFO_78FC
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_IN_VEC2_UV_7F0F
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_FLOAT_Y_TEXTURE_TY_UV_R_16_0_06ED
               SF2C_SHADERS_S_VEC2_C_TEXTURE_TUV_UV_RG_128_B700
               SF2C_SHADERS_S_O_VEC4_CLAMP_VEC3_Y_1_402_C__758E
               SF2C_SHADERS_S_N_E09D;
    }
    // ---- post passes before the present (Video tab "Anti-aliasing" / "Upscaler"): exec.c vk_present_draw.
    // Each draws a fullscreen triangle into an image the size of its output; push constants: two vec4.
    if (!strcmp(name, SF2C_SHADERS_S_POST_VERT_500E)) {
        *stage = STAGE_VERTEX;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC2_P_VEC2_GL_VERTEXINDEX_1_EF34
               SF2C_SHADERS_S_GL_POSITION_VEC4_P_2_0_1_0_0_AD79
               SF2C_SHADERS_S_N_E09D;
    }
    // SMAA 1x (third_party/smaa, runtime/nvn/smaa_data.c): the reference SMAA.hlsl in its GLSL mode, High preset,
    // with an entry point per pass. a = SMAA_RT_METRICS (1 / size, size). The vertex-stage offset helpers run per
    // pixel here. Luma edges (t0 = colour) -> blending weights (t0 = edges, t1 = area, t2 = search) -> neighbourhood
    // blending (t0 = colour, t1 = weights). Vulkan's texture origin is top-left, as SMAA expects (no flips).
    if (!strncmp(name, SF2C_SHADERS_S_SMAA_0B5B, SF2C_SHADERS_N_5_EF2D)) {
        *stage = STAGE_FRAGMENT;
        static char *src[SF2C_SHADERS_N_3_4E07];
        int k = !strcmp(name, SF2C_SHADERS_S_SMAA_EDGE_FRAG_9084) ? 0 : !strcmp(name, SF2C_SHADERS_S_SMAA_WEIGHT_FRAG_BA04) ? 1 : !strcmp(name, SF2C_SHADERS_S_SMAA_BLEND_FRAG_A526) ? SF2C_SHADERS_N_2_D473 : -1;
        if (k < 0) return NULL;
        if (!src[k]) {
            extern const char smaa_hlsl_src[];
            static const char *head =
                SF2C_SHADERS_S_VERSION_450_N_EA62
                SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_1FFD
                SF2C_SHADERS_S_DEFINE_SMAA_RT_METRICS_PC_A__2DF3
                SF2C_SHADERS_S_DEFINE_SMAA_GLSL_4_N_0DCF
                SF2C_SHADERS_S_DEFINE_SMAA_PRESET_HIGH_N_4ADD;
            static const char *mains[SF2C_SHADERS_N_3_4E07] = {
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_CC6D
                SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
                SF2C_SHADERS_S_VOID_MAIN_N_6682
                SF2C_SHADERS_S_VEC2_UV_GL_FRAGCOORD_XY_SMAA_486E
                SF2C_SHADERS_S_VEC4_OFF_3_N_86B6
                SF2C_SHADERS_S_SMAAEDGEDETECTIONVS_UV_OFF_N_F5CA
                SF2C_SHADERS_S_O_VEC4_SMAALUMAEDGEDETECTION_0B9D
                SF2C_SHADERS_S_N_E09D,
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_CC6D
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_1_UNIFO_66EC
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_2_UNIFO_1D28
                SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
                SF2C_SHADERS_S_VOID_MAIN_N_6682
                SF2C_SHADERS_S_VEC2_UV_GL_FRAGCOORD_XY_SMAA_0210
                SF2C_SHADERS_S_VEC4_OFF_3_N_86B6
                SF2C_SHADERS_S_SMAABLENDINGWEIGHTCALCULATIO_35DE
                SF2C_SHADERS_S_O_SMAABLENDINGWEIGHTCALCULAT_A202
                SF2C_SHADERS_S_N_E09D,
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_CC6D
                SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_1_UNIFO_66EC
                SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
                SF2C_SHADERS_S_VOID_MAIN_N_6682
                SF2C_SHADERS_S_VEC2_UV_GL_FRAGCOORD_XY_SMAA_486E
                SF2C_SHADERS_S_VEC4_OFF_N_9209
                SF2C_SHADERS_S_SMAANEIGHBORHOODBLENDINGVS_U_359C
                SF2C_SHADERS_S_O_VEC4_SMAANEIGHBORHOODBLEND_3B15
                SF2C_SHADERS_S_N_E09D};
            size_t n = strlen(head) + strlen(smaa_hlsl_src) + strlen(mains[k]) + 1;
            src[k] = malloc(n);
            snprintf(src[k], n, SF2C_SHADERS_S_S_S_S_DA90, head, smaa_hlsl_src, mains[k]);
        }
        return src[k];
    }
    // AMD FidelityFX Super Resolution 1 (MIT), EASU: edge-adaptive upscale. a = (in / out scale xy, offset zw),
    // b.xy = input size. 12 taps around the source pixel, direction / edge length from luma, Lanczos-2-like
    // weights stretched along the edge, clamped to the nearest 4 (no ringing).
    if (!strcmp(name, SF2C_SHADERS_S_EASU_FRAG_E8DD)) {
        *stage = STAGE_FRAGMENT;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_F635
               SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_1FFD
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
               SF2C_SHADERS_S_VEC3_TAP_IVEC2_P_RETURN_TEXE_8E9B
               SF2C_SHADERS_S_FLOAT_LUM_VEC3_C_RETURN_C_G__1A47
               SF2C_SHADERS_S_VOID_SETF_INOUT_VEC2_DIR_INO_9FCA
               SF2C_SHADERS_S_FLOAT_DX_LD_LB_DY_LE_LA_N_AA36
               SF2C_SHADERS_S_FLOAT_LX_CLAMP_ABS_DX_MAX_MA_B49A
               SF2C_SHADERS_S_FLOAT_LY_CLAMP_ABS_DY_MAX_MA_C29A
               SF2C_SHADERS_S_DIR_VEC2_DX_DY_W_N_BED7
               SF2C_SHADERS_S_LEN_LX_LX_LY_LY_W_N_72AD
               SF2C_SHADERS_S_N_E09D
               SF2C_SHADERS_S_VOID_TAPF_INOUT_VEC3_AC_INOU_320A
               SF2C_SHADERS_S_VEC2_V_VEC2_DOT_OFF_DIR_DOT__940B
               SF2C_SHADERS_S_FLOAT_D2_MIN_DOT_V_V_CLP_N_278F
               SF2C_SHADERS_S_FLOAT_WB_0_4_D2_1_0_WA_LOB_D_4B37
               SF2C_SHADERS_S_WB_WB_WA_WA_N_D341
               SF2C_SHADERS_S_FLOAT_W_1_5625_WB_0_5625_WA__6BFD
               SF2C_SHADERS_S_AC_C_W_AW_W_N_80F8
               SF2C_SHADERS_S_N_E09D
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC2_PP_FLOOR_GL_FRAGCOORD_X_1B77
               SF2C_SHADERS_S_VEC2_FPF_FLOOR_PP_PP_FPF_N_C175
               SF2C_SHADERS_S_IVEC2_FP_IVEC2_FPF_N_DACF
               SF2C_SHADERS_S_B_C_N_1C25
               SF2C_SHADERS_S_E_F_G_H_N_E3AF
               SF2C_SHADERS_S_I_J_K_L_N_30F6
               SF2C_SHADERS_S_N_O_N_F800
               SF2C_SHADERS_S_VEC3_BC_TAP_FP_IVEC2_0_1_CC__F4D1
               SF2C_SHADERS_S_VEC3_EC_TAP_FP_IVEC2_1_0_FC__6B5C
               SF2C_SHADERS_S_VEC3_IC_TAP_FP_IVEC2_1_1_JC__D24E
               SF2C_SHADERS_S_VEC3_NC_TAP_FP_IVEC2_0_2_OC__C6A6
               SF2C_SHADERS_S_FLOAT_BL_LUM_BC_CL_LUM_CC_EL_2171
               SF2C_SHADERS_S_FLOAT_IL_LUM_IC_JL_LUM_JC_KL_AE7F
               SF2C_SHADERS_S_VEC2_DIR_VEC2_0_0_N_87E5
               SF2C_SHADERS_S_FLOAT_LEN_0_0_N_0ED0
               SF2C_SHADERS_S_SETF_DIR_LEN_1_0_PP_X_1_0_PP_2D2D
               SF2C_SHADERS_S_SETF_DIR_LEN_PP_X_1_0_PP_Y_C_74A4
               SF2C_SHADERS_S_SETF_DIR_LEN_1_0_PP_X_PP_Y_F_320D
               SF2C_SHADERS_S_SETF_DIR_LEN_PP_X_PP_Y_GL_JL_C127
               SF2C_SHADERS_S_FLOAT_DR_DOT_DIR_DIR_N_016C
               SF2C_SHADERS_S_BOOL_ZRO_DR_1_0_32768_0_N_D364
               SF2C_SHADERS_S_DIR_ZRO_VEC2_1_0_0_0_DIR_INV_A153
               SF2C_SHADERS_S_LEN_LEN_0_5_LEN_LEN_N_2CAC
               SF2C_SHADERS_S_FLOAT_STRETCH_DOT_DIR_DIR_MA_FA7C
               SF2C_SHADERS_S_VEC2_LEN2_VEC2_1_0_STRETCH_1_9358
               SF2C_SHADERS_S_FLOAT_LOB_0_5_0_29_LEN_CLP_1_BE44
               SF2C_SHADERS_S_VEC3_MN_MIN_MIN_FC_GC_MIN_JC_3182
               SF2C_SHADERS_S_VEC3_AC_VEC3_0_0_N_6CA3
               SF2C_SHADERS_S_FLOAT_AW_0_0_N_B92D
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_0_0_1_0_PP_D_6572
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_1_0_PP_D_BFBF
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_1_0_PP_D_687C
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_0_0_1_0_PP_D_87A3
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_0_0_0_0_PP_D_9814
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_0_0_PP_D_CA07
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_1_0_PP_D_0511
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_2_0_1_0_PP_D_BCEF
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_2_0_0_0_PP_D_7BEA
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_0_0_PP_D_735B
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_1_0_2_0_PP_D_41DB
               SF2C_SHADERS_S_TAPF_AC_AW_VEC2_0_0_2_0_PP_D_0199
               SF2C_SHADERS_S_O_VEC4_MIN_MX_MAX_MN_AC_AW_1_D378
               SF2C_SHADERS_S_N_E09D;
    }
    // FSR 1 RCAS: contrast-adaptive sharpening of the EASU output (same size). a.x = sharpness (exp2(-stops)),
    // b.xy = image size. The lobe is limited so the result stays inside the neighbours' range; noise is damped.
    if (!strcmp(name, SF2C_SHADERS_S_RCAS_FRAG_2A91)) {
        *stage = STAGE_FRAGMENT;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_SET_0_BINDING_0_UNIFO_F635
               SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_1FFD
               SF2C_SHADERS_S_LAYOUT_LOCATION_0_OUT_VEC4_O_2613
               SF2C_SHADERS_S_VEC3_TAP_IVEC2_P_RETURN_TEXE_8E9B
               SF2C_SHADERS_S_FLOAT_LUM_VEC3_C_RETURN_C_G__1A47
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_IVEC2_IP_IVEC2_GL_FRAGCOORD__C1A1
               SF2C_SHADERS_S_B_N_11E9
               SF2C_SHADERS_S_D_E_F_N_A12A
               SF2C_SHADERS_S_H_N_C206
               SF2C_SHADERS_S_VEC3_B_TAP_IP_IVEC2_0_1_D_TA_CF08
               SF2C_SHADERS_S_FLOAT_BL_LUM_B_DL_LUM_D_EL_L_9AC4
               SF2C_SHADERS_S_FLOAT_NZ_0_25_BL_DL_FL_HL_EL_9376
               SF2C_SHADERS_S_FLOAT_RNG_MAX_MAX_MAX_BL_DL__DAAA
               SF2C_SHADERS_S_NZ_1_0_0_5_CLAMP_ABS_NZ_MAX__D768
               SF2C_SHADERS_S_VEC3_MN4_MIN_MIN_B_D_MIN_F_H_9C44
               SF2C_SHADERS_S_VEC3_HITMIN_MIN_MN4_E_MAX_4__512A
               SF2C_SHADERS_S_VEC3_HITMAX_1_0_MAX_MX4_E_MI_46E3
               SF2C_SHADERS_S_VEC3_LOBERGB_MAX_HITMIN_HITM_1C19
               SF2C_SHADERS_S_FLOAT_LOBE_MAX_0_25_1_0_16_0_8A15
               SF2C_SHADERS_S_O_VEC4_LOBE_B_D_F_H_E_4_0_LO_CF5C
               SF2C_SHADERS_S_N_E09D;
    }
    if (!strcmp(name, SF2C_SHADERS_S_CLEAR_VERT_6EF9)) {
        *stage = STAGE_VERTEX;
        return SF2C_SHADERS_S_VERSION_450_N_EA62
               SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_3A7C
               SF2C_SHADERS_S_VOID_MAIN_N_6682
               SF2C_SHADERS_S_VEC2_P_VEC2_GL_VERTEXINDEX_1_EF34
               SF2C_SHADERS_S_GL_POSITION_VEC4_P_2_0_1_0_P_00A3
               SF2C_SHADERS_S_N_E09D;
    }
    // clear.frag.<f|i|u><index>: writes the push-constant colour to colour target <index>
    if (!strncmp(name, SF2C_SHADERS_S_CLEAR_FRAG_1E54, SF2C_SHADERS_N_11_4FC8)) {
        *stage = STAGE_FRAGMENT;
        static __thread char buf[SF2C_SHADERS_N_512_94F8];
        char k = name[SF2C_SHADERS_N_11_4FC8];
        int idx = atoi(name + SF2C_SHADERS_N_12_6B51);
        const char *ty = k == SF2C_SHADERS_CH_I_088D ? SF2C_SHADERS_S_IVEC4_F360 : k == SF2C_SHADERS_CH_U_8E20 ? SF2C_SHADERS_S_UVEC4_1DCC : SF2C_SHADERS_S_VEC4_B997;
        const char *cv = k == SF2C_SHADERS_CH_I_088D ? SF2C_SHADERS_S_FLOATBITSTOINT_PC_C_AE17 : k == SF2C_SHADERS_CH_U_8E20 ? SF2C_SHADERS_S_FLOATBITSTOUINT_PC_C_0DB0 : SF2C_SHADERS_S_PC_C_E225;
        snprintf(buf, sizeof buf,
                 SF2C_SHADERS_S_VERSION_450_N_EA62
                 SF2C_SHADERS_S_LAYOUT_PUSH_CONSTANT_UNIFORM_3A7C
                 SF2C_SHADERS_S_LAYOUT_LOCATION_D_OUT_S_O_N_438F
                 SF2C_SHADERS_S_VOID_MAIN_O_S_N_8ADE, idx, ty, cv);
        return buf;
    }
    if (!strcmp(name, SF2C_SHADERS_S_CLEAR_FRAG_NONE_2562)) {
        *stage = STAGE_FRAGMENT;
        return SF2C_SHADERS_S_VERSION_450_NVOID_MAIN_N_C58F;
    }
    return NULL;
}

VkShaderModule vk_builtin_shader(const char *name) {
    static struct { char name[SF2C_SHADERS_N_32_E29C]; VkShaderModule m; } cache[SF2C_SHADERS_N_64_A68B];
    static int n;
    for (int i = 0; i < n; i++) if (!strcmp(cache[i].name, name)) return cache[i].m;
    int stage = 0;
    const char *src = builtin_src(name, &stage);
    if (!src) FATAL(SF2C_SHADERS_S_NO_BUILT_IN_SHADER_S_N_80A5, name);
    VkShaderModule m = vk_compile_glsl(src, stage, name);
    if (!m) FATAL(SF2C_SHADERS_S_BUILT_IN_SHADER_S_FAILED_TO__463D, name);
    if (n < SF2C_SHADERS_N_64_A68B) { snprintf(cache[n].name, sizeof cache[n].name, SF2C_SHADERS_S_S_0030, name); cache[n++].m = m; }
    return m;
}
