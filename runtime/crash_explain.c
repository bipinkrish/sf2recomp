#include "constants/core.h"
// A fatal error, explained to the player: the crash dialog (plat_fatal_notice) says what went wrong in plain words
// and what to do about it; the technical lines (FATAL / signal, backtrace, registers) stay in the log for bug reports.
// Known causes are recognised from what the runtime knows when it stops: the fatal line, where the crash was (the
// game code, LAS Recomp itself, the graphics driver), and the game files.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "platform.h"
#include "rt.h"

static char g_line[SF2C_CRASH_EXPLAIN_N_512_94F8];   // the first fatal line (FATAL message / "signal 11 accessing 0x10")
static uint64_t g_pc;      // a crash's host pc (0: a FATAL)
int rt_romfs_files = -1;   // the game's own RomFS files (runtime/services/fs.c; -1: not read yet)
enum { ROMFS_FILES = SF2C_CRASH_EXPLAIN_N_3836_395A };  // (every supported version: runtime/ui/launcher.mm counts the same)

void rt_fatal_msg(const char *fmt, ...) {
    char b[SF2C_CRASH_EXPLAIN_N_512_94F8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    fprintf(stderr, SF2C_CRASH_EXPLAIN_S_FATAL_S_1463, b);
    if (!g_line[0]) snprintf(g_line, sizeof g_line, SF2C_CRASH_EXPLAIN_S_S_0030, b);
}

void rt_crash_fault(const char *what, uint64_t fa, uint64_t pc) {
    if (!g_line[0]) snprintf(g_line, sizeof g_line, SF2C_CRASH_EXPLAIN_S_S_ACCESSING_LLX_35BC, what, (unsigned long long)fa);
    if (!g_pc) g_pc = pc;
}

static bool has(const char *s) { return strstr(g_line, s) != NULL; }

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);  // runtime/dispatch.c: inside the game code?

static bool contains_nocase(const char *hay, const char *needle) {  // (no strcasestr on Windows)
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (!needle[i]) return true;
    }
    return false;
}

// The graphics driver's own code (the crash was inside it, not in LAS Recomp): Vulkan drivers on Windows / Linux,
// Apple's Metal driver
static bool in_gpu_driver(uint64_t pc) {
    char m[SF2C_CRASH_EXPLAIN_N_512_94F8];
    if (!pc || !plat_module_name(pc, m, sizeof m)) return false;
    static const char *const drivers[] = {SF2C_CRASH_EXPLAIN_S_NVOGLV_4DD6, SF2C_CRASH_EXPLAIN_S_NVGPUCOMP_6386, SF2C_CRASH_EXPLAIN_S_NVWGF_6E21, SF2C_CRASH_EXPLAIN_S_LIBNVIDIA_E589, SF2C_CRASH_EXPLAIN_S_AMDVLK_AE2B, SF2C_CRASH_EXPLAIN_S_AMDXC_4674, SF2C_CRASH_EXPLAIN_S_ATIUMD_7871,
                                          SF2C_CRASH_EXPLAIN_S_ATIG6_2D80, SF2C_CRASH_EXPLAIN_S_IGVK_5036, SF2C_CRASH_EXPLAIN_S_IGC64_840B, SF2C_CRASH_EXPLAIN_S_IGD_3846, SF2C_CRASH_EXPLAIN_S_LIBVULKAN_RADEON_72CB, SF2C_CRASH_EXPLAIN_S_LIBVULKAN_INTEL_8CC6,
                                          SF2C_CRASH_EXPLAIN_S_LIBVULKAN_NOUVEAU_C2A5, SF2C_CRASH_EXPLAIN_S_LIBVULKAN_LVP_CCB8, SF2C_CRASH_EXPLAIN_S_LIBVULKAN_VIRTIO_6C5D, SF2C_CRASH_EXPLAIN_S_AGXMETAL_1FEE,
                                          SF2C_CRASH_EXPLAIN_S_MTLCOMPILER_FEFD, SF2C_CRASH_EXPLAIN_S_LIBLLVM_5B76, SF2C_CRASH_EXPLAIN_S_RADEONSI_FA32};
    for (size_t i = 0; i < sizeof drivers / sizeof *drivers; i++)
        if (contains_nocase(m, drivers[i])) return true;
    return false;
}

void rt_crash_explain(char *out, size_t n, const char *log_path) {
    static const char *const report = SF2C_CRASH_EXPLAIN_S_PLEASE_REPORT_IT_AS_A_GITHUB_6765
                                      SF2C_CRASH_EXPLAIN_S_YOUR_PROGRESS_UP_TO_YOUR_LAS_2EBF;
    char head[SF2C_CRASH_EXPLAIN_N_160_A512], what[SF2C_CRASH_EXPLAIN_N_400_26D2];
    if (rt_romfs_files >= 0 && rt_romfs_files < ROMFS_FILES) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_YOUR_GAME_FILES_ARE_INCOMPLE_7446);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_FOUND_D_OF_THE_GA_5A79
                 SF2C_CRASH_EXPLAIN_S_ONE_THAT_S_MISSING_COPY_YOUR_2F5B
                 SF2C_CRASH_EXPLAIN_S_START_SCREEN_GAME_FILES_47B2, rt_romfs_files, (int)ROMFS_FILES);
    } else if (has(SF2C_CRASH_EXPLAIN_S_DOESN_T_FIT_ITS_LANGUAGE_2493)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_YOUR_GAME_FILES_TEXT_HAS_BEE_38B5);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_THE_GAME_S_TEXT_FOR_THIS_LAN_9258
                 SF2C_CRASH_EXPLAIN_S_PATCH_AND_THE_GAME_STOPPED_A_6AC9
                 SF2C_CRASH_EXPLAIN_S_THE_START_SCREEN_GAME_FILES__0DB7);
    } else if (has(SF2C_CRASH_EXPLAIN_S_BUILT_FOR_ANOTHER_VERSION_4EC9)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_THE_GAME_CODE_WAS_BUILT_FOR__FB70);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_START_LAS_RECOMP_AGAIN_THE_S_47CC
                 SF2C_CRASH_EXPLAIN_S_FILES_BY_ITSELF_E3B9);
    } else if (has(SF2C_CRASH_EXPLAIN_S_UNSUPPORTED_VERSION_OF_THE_G_987E)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_WRONG_VERSION_OF_THE_GAME_9A5A);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_NEEDS_VERSION_1_0_6CBC
                 SF2C_CRASH_EXPLAIN_S_FILES_ON_THE_START_SCREEN_GA_AB31);
    } else if (has(SF2C_CRASH_EXPLAIN_S_NO_FONT_FOR_THE_GAME_S_TEXT_FF8F)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_NO_FONT_FOR_THE_GAME_S_TEXT_0995);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_IMPORT_THE_SWITCH_FONTS_IN_S_469A
                 SF2C_CRASH_EXPLAIN_S_COMES_WITH_A_FONT_OF_ITS_OWN_792E);
    } else if (has(SF2C_CRASH_EXPLAIN_S_GET_TERMINATE_CB47) || has(SF2C_CRASH_EXPLAIN_S_UNCAUGHT_C_EXCEPTION_9ABD)) {  // (the game threw: recompiled code can't
                                                                         // unwind; runtime/hle/warp.c rt_cxa_throw)
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_THE_GAME_RAN_INTO_AN_ERROR_I_C3D9);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_THIS_IS_A_BUG_IN_LAS_RECOMP__808F, report);
    } else if (in_gpu_driver(g_pc)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_YOUR_GRAPHICS_DRIVER_CRASHED_32BA);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_UPDATE_YOUR_GRAPHICS_DRIVER__0F7B
                 SF2C_CRASH_EXPLAIN_S_HAPPENING_PLEASE_REPORT_IT_W_2754);
    } else if (has(SF2C_CRASH_EXPLAIN_S_VK_BDFD) || has(SF2C_CRASH_EXPLAIN_S_VULKAN_F59E)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_GRAPHICS_DRIVER_ERROR_D10F);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_NEEDS_A_GRAPHICS__E401
                 SF2C_CRASH_EXPLAIN_S_YOUR_GPU_NVIDIA_AMD_OR_INTEL_BDE0);
    } else if (has(SF2C_CRASH_EXPLAIN_S_OUT_OF_MEMORY_8C2B) || has(SF2C_CRASH_EXPLAIN_S_CANNOT_ALLOCATE_E9E6) || has(SF2C_CRASH_EXPLAIN_S_ALLOC_FAILED_689D)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_OUT_OF_MEMORY_B8CB);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_CLOSE_OTHER_PROGRAMS_AND_TRY_015E);
    } else if (g_pc && rt_host_fn_name(g_pc, NULL)) {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_THE_GAME_CRASHED_A075);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_THIS_IS_A_BUG_IN_LAS_RECOMP__808F, report);
    } else {
        snprintf(head, sizeof head, SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_STOPPED_BECAUSE_O_7D62);
        snprintf(what, sizeof what, SF2C_CRASH_EXPLAIN_S_S_0030, report);
    }
    fprintf(stderr, SF2C_CRASH_EXPLAIN_S_CRASH_S_N_02D3, head);
    if (log_path && *log_path) snprintf(out, n, SF2C_CRASH_EXPLAIN_S_S_N_N_S_N_NLOG_FILE_N_S_9B65, head, what, log_path);
    else snprintf(out, n, SF2C_CRASH_EXPLAIN_S_S_N_N_S_2828, head, what);
}
