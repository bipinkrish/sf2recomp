// Fixed values for this subsystem. Edit definitions here.
#pragma once
// runtime/cpu.h:9
#define SF2C_CPU_N_16_B17E 16
// runtime/cpu.h:10
#define SF2C_CPU_N_32_E29C 32
// runtime/cpu.h:22
#define SF2C_CPU_N_512_94F8 512
// runtime/cpu.h:22
#define SF2C_CPU_N_352_9A72 352
// runtime/cpu.h:26
#define SF2C_CPU_N_256_51E8 256
// runtime/cpu.h:26
#define SF2C_CPU_S_SP_0674 "sp"
// runtime/cpu.h:27
#define SF2C_CPU_N_264_BBA5 264
// runtime/cpu.h:27
#define SF2C_CPU_S_PC_F2CD "pc"
// runtime/cpu.h:28
#define SF2C_CPU_N_272_1C6C 272
// runtime/cpu.h:28
#define SF2C_CPU_S_NZCV_B3D5 "nzcv"
// runtime/cpu.h:29
#define SF2C_CPU_N_320_8882 320
// runtime/cpu.h:29
#define SF2C_CPU_S_FPCR_9C54 "fpcr"
// runtime/cpu.h:30
#define SF2C_CPU_N_328_2452 328
// runtime/cpu.h:30
#define SF2C_CPU_S_FPSR_DC79 "fpsr"
// runtime/cpu.h:31
#define SF2C_CPU_S_V_D1A4 "v"
// runtime/cpu.h:43
#define SF2C_CPU_N_1U_B8E6 1u
// runtime/cpu.h:53
#define SF2C_CPU_S_MRS_0_FPCR_1621 "mrs %0, fpcr"
// runtime/cpu.h:53
#define SF2C_CPU_S_R_49F7 "=r"
// runtime/cpu.h:54
#define SF2C_CPU_S_MRS_0_FPSR_8F99 "mrs %0, fpsr"
// runtime/cpu.h:55
#define SF2C_CPU_S_MSR_FPCR_0_2CE9 "msr fpcr, %0"
// runtime/cpu.h:55
#define SF2C_CPU_S_R_2659 "r"
// runtime/cpu.h:56
#define SF2C_CPU_S_MSR_FPSR_0_4330 "msr fpsr, %0"
// runtime/cpu.h:75
#define SF2C_CPU_N_30_624B 30
// runtime/cpu.h:100
#define SF2C_CPU_N_0ULL_DF1B 0ull
// runtime/cpu.h:122
#define SF2C_CPU_N_64_A68B 64
// runtime/cpu.h:149
#define SF2C_CPU_N_31_EB1E 31
// runtime/cpu.h:151
#define SF2C_CPU_N_29_3513 29
// runtime/cpu.h:152
#define SF2C_CPU_N_28_59E1 28
// runtime/cpu.h:173
#define SF2C_CPU_N_63_DA4E 63
// runtime/cpu.h:222
#define SF2C_CPU_N_0X00FF00FFU_9E9C 0x00ff00ffu
// runtime/cpu.h:224
#define SF2C_CPU_N_0X00FF00FF00FF00FFULL_1ECA 0x00ff00ff00ff00ffull
// runtime/crash_explain.c:13
#define SF2C_CRASH_EXPLAIN_N_512_94F8 512
// runtime/crash_explain.c:16
#define SF2C_CRASH_EXPLAIN_N_3836_395A 3836
// runtime/crash_explain.c:24
#define SF2C_CRASH_EXPLAIN_S_FATAL_S_1463 "FATAL: %s"
// runtime/crash_explain.c:25
#define SF2C_CRASH_EXPLAIN_S_S_0030 "%s"
// runtime/crash_explain.c:29
#define SF2C_CRASH_EXPLAIN_S_S_ACCESSING_LLX_35BC "%s accessing %#llx"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_NVOGLV_4DD6 "nvoglv"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_NVGPUCOMP_6386 "nvgpucomp"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_NVWGF_6E21 "nvwgf"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_LIBNVIDIA_E589 "libnvidia"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_AMDVLK_AE2B "amdvlk"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_AMDXC_4674 "amdxc"
// runtime/crash_explain.c:51
#define SF2C_CRASH_EXPLAIN_S_ATIUMD_7871 "atiumd"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_ATIG6_2D80 "atig6"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_IGVK_5036 "igvk"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_IGC64_840B "igc64"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_IGD_3846 "igd"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_LIBVULKAN_RADEON_72CB "libvulkan_radeon"
// runtime/crash_explain.c:52
#define SF2C_CRASH_EXPLAIN_S_LIBVULKAN_INTEL_8CC6 "libvulkan_intel"
// runtime/crash_explain.c:53
#define SF2C_CRASH_EXPLAIN_S_LIBVULKAN_NOUVEAU_C2A5 "libvulkan_nouveau"
// runtime/crash_explain.c:53
#define SF2C_CRASH_EXPLAIN_S_LIBVULKAN_LVP_CCB8 "libvulkan_lvp"
// runtime/crash_explain.c:53
#define SF2C_CRASH_EXPLAIN_S_LIBVULKAN_VIRTIO_6C5D "libvulkan_virtio"
// runtime/crash_explain.c:53
#define SF2C_CRASH_EXPLAIN_S_AGXMETAL_1FEE "AGXMetal"
// runtime/crash_explain.c:54
#define SF2C_CRASH_EXPLAIN_S_MTLCOMPILER_FEFD "MTLCompiler"
// runtime/crash_explain.c:54
#define SF2C_CRASH_EXPLAIN_S_LIBLLVM_5B76 "libLLVM"
// runtime/crash_explain.c:54
#define SF2C_CRASH_EXPLAIN_S_RADEONSI_FA32 "radeonsi"
// runtime/crash_explain.c:61
#define SF2C_CRASH_EXPLAIN_S_PLEASE_REPORT_IT_AS_A_GITHUB_6765 "Please report it as a GitHub issue and attach the log file below. "
// runtime/crash_explain.c:62
#define SF2C_CRASH_EXPLAIN_S_YOUR_PROGRESS_UP_TO_YOUR_LAS_2EBF "Your progress up to your last save or autosave is safe."
// runtime/crash_explain.c:63
#define SF2C_CRASH_EXPLAIN_N_160_A512 160
// runtime/crash_explain.c:63
#define SF2C_CRASH_EXPLAIN_N_400_26D2 400
// runtime/crash_explain.c:65
#define SF2C_CRASH_EXPLAIN_S_YOUR_GAME_FILES_ARE_INCOMPLE_7446 "Your game files are incomplete"
// runtime/crash_explain.c:66
#define SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_FOUND_D_OF_THE_GA_5A79 "LAS Recomp found %d of the game's %d files, and the game stopped when it needed "
// runtime/crash_explain.c:67
#define SF2C_CRASH_EXPLAIN_S_ONE_THAT_S_MISSING_COPY_YOUR_2F5B "one that's missing. Copy your game files again so none are missing, then set them up again on the "
// runtime/crash_explain.c:68
#define SF2C_CRASH_EXPLAIN_S_START_SCREEN_GAME_FILES_47B2 "start screen (Game Files)."
// runtime/crash_explain.c:69
#define SF2C_CRASH_EXPLAIN_S_DOESN_T_FIT_ITS_LANGUAGE_2493 "doesn't fit its language"
// runtime/crash_explain.c:70
#define SF2C_CRASH_EXPLAIN_S_YOUR_GAME_FILES_TEXT_HAS_BEE_38B5 "Your game files' text has been changed"
// runtime/crash_explain.c:71
#define SF2C_CRASH_EXPLAIN_S_THE_GAME_S_TEXT_FOR_THIS_LAN_9258 "The game's text for this language comes from another language (a translation "
// runtime/crash_explain.c:72
#define SF2C_CRASH_EXPLAIN_S_PATCH_AND_THE_GAME_STOPPED_A_6AC9 "patch), and the game stopped at a question it can't show that way. Set up unmodified game files on "
// runtime/crash_explain.c:73
#define SF2C_CRASH_EXPLAIN_S_THE_START_SCREEN_GAME_FILES__0DB7 "the start screen (Game Files). To play in another language, pick it in Settings (Language)."
// runtime/crash_explain.c:74
#define SF2C_CRASH_EXPLAIN_S_BUILT_FOR_ANOTHER_VERSION_4EC9 "built for another version"
// runtime/crash_explain.c:75
#define SF2C_CRASH_EXPLAIN_S_THE_GAME_CODE_WAS_BUILT_FOR__FB70 "The game code was built for other game files or another version of LAS Recomp"
// runtime/crash_explain.c:76
#define SF2C_CRASH_EXPLAIN_S_START_LAS_RECOMP_AGAIN_THE_S_47CC "Start LAS Recomp again: the start screen rebuilds the game code for these game "
// runtime/crash_explain.c:77
#define SF2C_CRASH_EXPLAIN_S_FILES_BY_ITSELF_E3B9 "files by itself."
// runtime/crash_explain.c:78
#define SF2C_CRASH_EXPLAIN_S_UNSUPPORTED_VERSION_OF_THE_G_987E "unsupported version of the game"
// runtime/crash_explain.c:79
#define SF2C_CRASH_EXPLAIN_S_WRONG_VERSION_OF_THE_GAME_9A5A "Wrong version of the game"
// runtime/crash_explain.c:80
#define SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_NEEDS_VERSION_1_0_6CBC "LAS Recomp needs version 1.0.0, 1.0.1 or 1.1.0 of the game. Set up those game "
// runtime/crash_explain.c:81
#define SF2C_CRASH_EXPLAIN_S_FILES_ON_THE_START_SCREEN_GA_AB31 "files on the start screen (Game Files)."
// runtime/crash_explain.c:82
#define SF2C_CRASH_EXPLAIN_S_NO_FONT_FOR_THE_GAME_S_TEXT_FF8F "no font for the game's text"
// runtime/crash_explain.c:83
#define SF2C_CRASH_EXPLAIN_S_NO_FONT_FOR_THE_GAME_S_TEXT_0995 "No font for the game's text"
// runtime/crash_explain.c:84
#define SF2C_CRASH_EXPLAIN_S_IMPORT_THE_SWITCH_FONTS_IN_S_469A "Import the Switch fonts in Settings (Game font), or reinstall LAS Recomp - it "
// runtime/crash_explain.c:85
#define SF2C_CRASH_EXPLAIN_S_COMES_WITH_A_FONT_OF_ITS_OWN_792E "comes with a font of its own."
// runtime/crash_explain.c:86
#define SF2C_CRASH_EXPLAIN_S_GET_TERMINATE_CB47 "get_terminate"
// runtime/crash_explain.c:86
#define SF2C_CRASH_EXPLAIN_S_UNCAUGHT_C_EXCEPTION_9ABD "uncaught C++ exception"
// runtime/crash_explain.c:88
#define SF2C_CRASH_EXPLAIN_S_THE_GAME_RAN_INTO_AN_ERROR_I_C3D9 "The game ran into an error it couldn't recover from"
// runtime/crash_explain.c:89
#define SF2C_CRASH_EXPLAIN_S_THIS_IS_A_BUG_IN_LAS_RECOMP__808F "This is a bug in LAS Recomp, not a problem with your game files or PC. %s"
// runtime/crash_explain.c:91
#define SF2C_CRASH_EXPLAIN_S_YOUR_GRAPHICS_DRIVER_CRASHED_32BA "Your graphics driver crashed"
// runtime/crash_explain.c:92
#define SF2C_CRASH_EXPLAIN_S_UPDATE_YOUR_GRAPHICS_DRIVER__0F7B "Update your graphics driver (NVIDIA, AMD or Intel) and try again. If it keeps "
// runtime/crash_explain.c:93
#define SF2C_CRASH_EXPLAIN_S_HAPPENING_PLEASE_REPORT_IT_W_2754 "happening, please report it with the log file below."
// runtime/crash_explain.c:94
#define SF2C_CRASH_EXPLAIN_S_VK_BDFD "VK_"
// runtime/crash_explain.c:94
#define SF2C_CRASH_EXPLAIN_S_VULKAN_F59E "Vulkan"
// runtime/crash_explain.c:95
#define SF2C_CRASH_EXPLAIN_S_GRAPHICS_DRIVER_ERROR_D10F "Graphics driver error"
// runtime/crash_explain.c:96
#define SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_NEEDS_A_GRAPHICS__E401 "LAS Recomp needs a graphics driver with Vulkan 1.3: install the latest driver for "
// runtime/crash_explain.c:97
#define SF2C_CRASH_EXPLAIN_S_YOUR_GPU_NVIDIA_AMD_OR_INTEL_BDE0 "your GPU (NVIDIA, AMD or Intel). If it keeps happening, please report it with the log file below."
// runtime/crash_explain.c:98
#define SF2C_CRASH_EXPLAIN_S_OUT_OF_MEMORY_8C2B "out of memory"
// runtime/crash_explain.c:98
#define SF2C_CRASH_EXPLAIN_S_CANNOT_ALLOCATE_E9E6 "cannot allocate"
// runtime/crash_explain.c:98
#define SF2C_CRASH_EXPLAIN_S_ALLOC_FAILED_689D "alloc failed"
// runtime/crash_explain.c:99
#define SF2C_CRASH_EXPLAIN_S_OUT_OF_MEMORY_B8CB "Out of memory"
// runtime/crash_explain.c:100
#define SF2C_CRASH_EXPLAIN_S_CLOSE_OTHER_PROGRAMS_AND_TRY_015E "Close other programs and try again."
// runtime/crash_explain.c:102
#define SF2C_CRASH_EXPLAIN_S_THE_GAME_CRASHED_A075 "The game crashed"
// runtime/crash_explain.c:105
#define SF2C_CRASH_EXPLAIN_S_LAS_RECOMP_STOPPED_BECAUSE_O_7D62 "LAS Recomp stopped because of an error"
// runtime/crash_explain.c:108
#define SF2C_CRASH_EXPLAIN_S_CRASH_S_N_02D3 "[crash] %s\n"
// runtime/crash_explain.c:109
#define SF2C_CRASH_EXPLAIN_S_S_N_N_S_N_NLOG_FILE_N_S_9B65 "%s\n\n%s\n\nLog file:\n%s"
// runtime/crash_explain.c:110
#define SF2C_CRASH_EXPLAIN_S_S_N_N_S_2828 "%s\n\n%s"
// runtime/developer.h:10
#define SF2C_DEVELOPER_N_256_51E8 256
// runtime/dispatch.c:32
#define SF2C_DISPATCH_S_SECTION_START_DATA_HLE_IMPLS_FFA0 "section$start$__DATA$__hle_impls"
// runtime/dispatch.c:33
#define SF2C_DISPATCH_S_SECTION_END_DATA_HLE_IMPLS_DA79 "section$end$__DATA$__hle_impls"
// runtime/dispatch.c:38
#define SF2C_DISPATCH_S_HLE_A_BC8C ".hle$a"
// runtime/dispatch.c:39
#define SF2C_DISPATCH_S_HLE_Z_14B5 ".hle$z"
// runtime/dispatch.c:44
#define SF2C_DISPATCH_S_LARECOMP_HLE_SKIP_8648 "LARECOMP_HLE_SKIP"
// runtime/dispatch.c:47
#define SF2C_DISPATCH_S_EMPTY_12AE ""
// runtime/dispatch.c:47
#define SF2C_DISPATCH_S_R_E649 "+r"
// runtime/dispatch.c:57
#define SF2C_DISPATCH_N_16ULL_001B 16ull
// runtime/dispatch.c:58
#define SF2C_DISPATCH_S_TOO_MANY_HLE_STUBS_N_B360 "too many HLE stubs\n"
// runtime/dispatch.c:67
#define SF2C_DISPATCH_N_16_B17E 16
// runtime/dispatch.c:74
#define SF2C_DISPATCH_S_UNIMPLEMENTED_HOST_FUNCTION__7D2E "unimplemented host function %s (guest lr=%llx)\n"
// runtime/dispatch.c:75
#define SF2C_DISPATCH_N_30_624B 30
// runtime/dispatch.c:80
#define SF2C_DISPATCH_N_64_A68B 64
// runtime/dispatch.c:87
#define SF2C_DISPATCH_S_GUEST_SYMBOL_S_NOT_FOUND_N_BD46 "guest symbol %s not found\n"
// runtime/dispatch.c:101
#define SF2C_DISPATCH_N_63_DA4E 63
// runtime/dispatch.c:101
#define SF2C_DISPATCH_N_63ULL_F414 63ull
// runtime/dispatch.c:102
#define SF2C_DISPATCH_S_HLE_HEAP_EXHAUSTED_N_2DDC "HLE heap exhausted\n"
// runtime/dispatch.c:150
#define SF2C_DISPATCH_S_NULL_CALL_64A9 "null-call"
// runtime/dispatch.c:151
#define SF2C_DISPATCH_S_NO_FUNCTION_AT_LLX_LR_LLX_S__A40F "no function at %llx (lr=%llx %s+%llx)\n"
// runtime/dispatch.c:151
#define SF2C_DISPATCH_S_EMPTY_15C5 "?"
// runtime/dispatch.c:189
#define SF2C_DISPATCH_N_32_E29C 32
// runtime/dispatch.c:209
#define SF2C_DISPATCH_N_0X100000_0A2A 0x100000
// runtime/dispatch.c:210
#define SF2C_DISPATCH_S_F_LLX_982B "f_%llx"
// runtime/dispatch.c:238
#define SF2C_DISPATCH_N_0X20_A766 0x20
// runtime/dispatch.c:238
#define SF2C_DISPATCH_N_0X7F_0AFB 0x7f
// runtime/dispatch.c:238
#define SF2C_DISPATCH_CH_EMPTY_5DE5 '?'
// runtime/dispatch.c:246
#define SF2C_DISPATCH_N_0X10_BF9C 0x10
// runtime/dispatch.c:246
#define SF2C_DISPATCH_N_0X20000_422F 0x20000
// runtime/dispatch.c:250
#define SF2C_DISPATCH_N_0X14_F942 0x14
// runtime/dispatch.c:252
#define SF2C_DISPATCH_CH_R_4859 '\r'
// runtime/dispatch.c:252
#define SF2C_DISPATCH_CH_N_AD65 '\n'
// runtime/dispatch.c:252
#define SF2C_DISPATCH_CH_EMPTY_730D ' '
// runtime/dispatch.c:264
#define SF2C_DISPATCH_N_96_7B1A 96
// runtime/dispatch.c:267
#define SF2C_DISPATCH_N_0X18_D47C 0x18
// runtime/dispatch.c:269
#define SF2C_DISPATCH_S_X_02D_S_LLX_OBJ_CLASS_S_S_S__C268 "  [x%02d] %s+%llx obj: class %s%s%s\n"
// runtime/dispatch.c:270
#define SF2C_DISPATCH_S_EMPTY_7299 "."
// runtime/dispatch.c:271
#define SF2C_DISPATCH_N_0X58_08FB 0x58
// runtime/dispatch.c:274
#define SF2C_DISPATCH_N_192_EB3B 192
// runtime/dispatch.c:276
#define SF2C_DISPATCH_S_X_02D_S_LLX_FIELD_0X_LLX_S_N_0076 "  [x%02d] %s+%llx field +0x%llx: '%s'\n"
// runtime/dispatch.c:281
#define SF2C_DISPATCH_S_HOST_BACKTRACE_GUEST_FUNCTIO_7967 "host backtrace (guest functions are f_<addr>):\n"
// runtime/dispatch.c:287
#define SF2C_DISPATCH_S_GUEST_SP_LLX_LR_LLX_S_0X_LLX_33BB "guest: sp=%llx lr=%llx (%s + 0x%llx)\n"
// runtime/dispatch.c:287
#define SF2C_DISPATCH_S_UNKNOWN_EB8B "unknown"
// runtime/dispatch.c:288
#define SF2C_DISPATCH_N_31_EB1E 31
// runtime/dispatch.c:289
#define SF2C_DISPATCH_S_X_2D_016LLX_S_FF3D "  x%-2d %016llx%s"
// runtime/dispatch.c:290
#define SF2C_DISPATCH_S_N_D7A5 "\n"
// runtime/dispatch.c:295
#define SF2C_DISPATCH_S_LARECOMP_HWBREAK_8AFC "LARECOMP_HWBREAK"
// runtime/dispatch.c:296
#define SF2C_DISPATCH_S_HWBREAK_STOPPED_PID_D_FROZEN_C10E "hwbreak: stopped pid %d (frozen; gdb -p %d now)\n"
// runtime/dispatch.c:297
#define SF2C_DISPATCH_N_3600_8B34 3600
// runtime/dispatch.c:306
#define SF2C_DISPATCH_S_CXA_THROW_DFEA "__cxa_throw"
// runtime/dispatch.c:308
#define SF2C_DISPATCH_S_THROW_GUEST_C_EXCEPTION_OBJ__ADA0 "[throw] guest C++ exception: obj %llx, type_info %llx, destructor %llx\n"
// runtime/dispatch.c:313
#define SF2C_DISPATCH_S_THROW_C_TYPE_S_N_1953 "[throw] C++ type '%s'\n"
// runtime/dispatch.c:316
#define SF2C_DISPATCH_S_EXCEPTION_C22C "exception"
// runtime/dispatch.c:322
#define SF2C_DISPATCH_S_UNDEFINED_INSTRUCTION_TRAP_A_D0DA "undefined instruction / trap at %llx\n"
// runtime/dispatch.c:327
#define SF2C_DISPATCH_S_GUEST_SDK_TRAP_AT_LLX_LR_LLX_15E1 "guest SDK trap at %llx (lr=%llx)\n"
// runtime/dispatch.c:332
#define SF2C_DISPATCH_S_BRK_U_AT_LLX_N_CCDD "brk #%u at %llx\n"
// runtime/dispatch.c:345
#define SF2C_DISPATCH_S_WATCH_PAGE_LLX_PROTECT_D_N_58AB "[watch] page %llx protect -> %d\n"
// runtime/dispatch.c:347
#define SF2C_DISPATCH_N_100_AD57 100
// runtime/dispatch.c:349
#define SF2C_DISPATCH_S_WATCH_PAGE_LLX_RELEASED_N_5CD7 "[watch] page %llx released\n"
// runtime/dispatch.c:356
#define SF2C_DISPATCH_S_LARECOMP_WATCH_ADDR_5B12 "LARECOMP_WATCH_ADDR"
// runtime/dispatch.c:357
#define SF2C_DISPATCH_CH_EMPTY_EE1B ','
// runtime/dispatch.c:386
#define SF2C_DISPATCH_S_S_0030 "%s"
// runtime/dispatch.c:388
#define SF2C_DISPATCH_S_WATCH_FA_LLX_PC_LLX_S_LR_LLX_5973 "[watch] fa %llx pc %llx (%s) lr %llx (%s+%llx)\n"
// runtime/dispatch.c:392
#define SF2C_DISPATCH_S_PROC_SELF_MAPS_D46C "/proc/self/maps"
// runtime/dispatch.c:392
#define SF2C_DISPATCH_S_R_2659 "r"
// runtime/dispatch.c:393
#define SF2C_DISPATCH_N_512_94F8 512
// runtime/dispatch.c:395
#define SF2C_DISPATCH_S_LX_LX_EE31 "%lx-%lx "
// runtime/dispatch.c:396
#define SF2C_DISPATCH_S_WATCH_WRITER_MAP_S_C932 "[watch] writer map: %s"
// runtime/dispatch.c:401
#define SF2C_DISPATCH_N_48_9801 48
// runtime/dispatch.c:406
#define SF2C_DISPATCH_S_36S_LLX_D2D9 "%.36s+%#llx"
// runtime/dispatch.c:411
#define SF2C_DISPATCH_S_WATCH_LLX_BY_S_N_B56C "[watch] +%#llx by %s\n"
// runtime/dispatch.c:422
#define SF2C_DISPATCH_S_N_S_ACCESSING_LLX_N_HOST_PC__A55E "\n*** %s accessing %#llx\n    host pc %llx (%s+%llx)\n    host lr %llx (%s)\n"
// runtime/exception.c:31
#define SF2C_EXCEPTION_N_30_624B 30
// runtime/exception.c:61
#define SF2C_EXCEPTION_S_EH_CAUGHT_TYPE_LLX_AT_CALL_L_C10C "[eh] caught type %llx at call %llx -> landing pad %llx (selector %llu)\n"
// runtime/exception.c:72
#define SF2C_EXCEPTION_S_CXA_BEGIN_CATCH_E934 "__cxa_begin_catch"
// runtime/exception.c:76
#define SF2C_EXCEPTION_S_CXA_END_CATCH_CE3B "__cxa_end_catch"
// runtime/fnprof.c:13
#define SF2C_FNPROF_N_18_4EC9 18
// runtime/fnprof.c:18
#define SF2C_FNPROF_N_256_51E8 256
// runtime/fnprof.c:27
#define SF2C_FNPROF_S_LARECOMP_FNPROF_BT_ACF6 "LARECOMP_FNPROF_BT"
// runtime/fnprof.c:29
#define SF2C_FNPROF_N_16_B17E 16
// runtime/fnprof.c:30
#define SF2C_FNPROF_CH_EMPTY_EE1B ','
// runtime/fnprof.c:30
#define SF2C_FNPROF_CH_EMPTY_730D ' '
// runtime/fnprof.c:36
#define SF2C_FNPROF_N_0X7000000000ULL_65B2 0x7000000000ull
// runtime/fnprof.c:36
#define SF2C_FNPROF_N_0X7800000000ULL_5ACA 0x7800000000ull
// runtime/fnprof.c:37
#define SF2C_FNPROF_N_0X100_BE6E 0x100
// runtime/fnprof.c:39
#define SF2C_FNPROF_S_FNPROF_BT_LLX_ENTERED_FROM_G_B5C1 "[fnprof-bt] %llx entered from guest lr %llx x0 %llx (vtable %llx, +0x100 %llx, [x0+8] %llx):\n"
// runtime/fnprof.c:48
#define SF2C_FNPROF_N_0X9E3779B1U_BCDD 0x9E3779B1u
// runtime/fnprof.c:67
#define SF2C_FNPROF_S_LARECOMP_FNPROF_CC00 "LARECOMP_FNPROF"
// runtime/fnprof.c:68
#define SF2C_FNPROF_S_LD_LD_CF3A "%ld:%ld"
// runtime/fnprof.c:71
#define SF2C_FNPROF_S_FNPROF_ON_AT_FRAME_LLU_N_0098 "[fnprof] on at frame %llu\n"
// runtime/fnprof.c:79
#define SF2C_FNPROF_S_LARECOMP_FNPROF_OUT_EA89 "LARECOMP_FNPROF_OUT"
// runtime/fnprof.c:79
#define SF2C_FNPROF_S_BUILD_FNPROF_TXT_A5F6 "build/fnprof.txt"
// runtime/fnprof.c:80
#define SF2C_FNPROF_S_W_7265 "w"
// runtime/fnprof.c:81
#define SF2C_FNPROF_S_LLX_LLU_N_670A "%llx %llu\n"
// runtime/fnprof.c:84
#define SF2C_FNPROF_S_FNPROF_D_FUNCTIONS_WRITTEN_S_9D4B "[fnprof] %d functions, written %s (frames %ld-%ld)\n"
// runtime/fnprof.c:91
#define SF2C_FNPROF_CH_0_59A9 '0'
// runtime/fnprof.c:91
#define SF2C_FNPROF_CH_EMPTY_4266 ':'
// runtime/game_addresses.c:19
#define SF2C_GAME_ADDRESSES_CH_9_E453 '9'
// runtime/game_addresses.c:19
#define SF2C_GAME_ADDRESSES_CH_0_59A9 '0'
// runtime/game_addresses.c:19
#define SF2C_GAME_ADDRESSES_CH_A_341F 'A'
// runtime/game_addresses.c:19
#define SF2C_GAME_ADDRESSES_N_10_4A44 10
// runtime/game_addresses.c:33
#define SF2C_GAME_ADDRESSES_N_0X60_3D6E 0x60
// runtime/game_addresses.c:34
#define SF2C_GAME_ADDRESSES_S_RB_46CA "rb"
// runtime/game_addresses.c:36
#define SF2C_GAME_ADDRESSES_S_NSO0_F632 "NSO0"
// runtime/game_addresses.c:38
#define SF2C_GAME_ADDRESSES_N_0X40_1AF3 0x40
// runtime/game_addresses.c:48
#define SF2C_GAME_ADDRESSES_N_1100_AB98 1100
// runtime/game_addresses.c:49
#define SF2C_GAME_ADDRESSES_S_S_EXEFS_MAIN_CD96 "%s/exefs/main"
// runtime/game_addresses.c:56
#define SF2C_GAME_ADDRESSES_S_S_EXEFS_2DFB "%s/exefs"
// runtime/game_addresses.c:57
#define SF2C_GAME_ADDRESSES_S_S_MAIN_3565 "%s/main"
// runtime/game_addresses.c:59
#define SF2C_GAME_ADDRESSES_S_THE_GAME_FILES_EXEFS_MAIN_IS_7B63 "the game files' exefs/main is missing or unreadable"
// runtime/game_addresses.c:64
#define SF2C_GAME_ADDRESSES_S_THESE_GAME_FILES_ARE_AN_UNSU_B1F9 "these game files are an unsupported version of the game (main build ID "
// runtime/game_addresses.c:65
#define SF2C_GAME_ADDRESSES_S_02X_02X_02X_02X_02X_02X_02X__70D4 "%02X%02X%02X%02X%02X%02X%02X%02X...): the recompiler needs a SF2 update v131072 (v1.1.0)"
// runtime/game_addresses.c:70
#define SF2C_GAME_ADDRESSES_S_GAME_VERSION_S_MAIN_BUILD_ID_242C "[game] version %s (main build ID %.16s, version %s)\n"
// runtime/game_addresses.h:8
#define SF2C_GAME_ADDRESSES_S_C_C2E8 "C"
// runtime/game_addresses.h:11
#define SF2C_GAME_ADDRESSES_N_16_B17E 16
// runtime/game_info.c:9
#define SF2C_GAME_INFO_N_17_4523 17
// runtime/game_info.c:10
#define SF2C_GAME_INFO_N_128_2747 128
// runtime/game_info.c:11
#define SF2C_GAME_INFO_N_4096_8B92 4096
// runtime/game_info.c:15
#define SF2C_GAME_INFO_S_S_S_22D8 "%s/%s"
// runtime/game_info.c:16
#define SF2C_GAME_INFO_S_RB_46CA "rb"
// runtime/game_info.c:25
#define SF2C_GAME_INFO_S_S_0030 "%s"
// runtime/game_info.c:28
#define SF2C_GAME_INFO_CH_EMPTY_0825 '/'
// runtime/game_info.c:34
#define SF2C_GAME_INFO_S_CONTROL_NACP_CE21 "control.nacp"
// runtime/game_info.c:36
#define SF2C_GAME_INFO_N_24_C235 24
// runtime/game_info.c:37
#define SF2C_GAME_INFO_N_0X3060_FE40 0x3060
// runtime/game_info.c:40
#define SF2C_GAME_INFO_N_16_B17E 16
// runtime/game_info.c:44
#define SF2C_GAME_INFO_N_0X20_A766 0x20
// runtime/game_info.c:44
#define SF2C_GAME_INFO_N_0X7F_0AFB 0x7f
// runtime/game_info.c:52
#define SF2C_GAME_INFO_S_S_S_33DC "%s - %s"
// runtime/game_info.c:53
#define SF2C_GAME_INFO_S_GAME_OFFICIAL_DISPLAY_VERSIO_3688 "[game] official display version %s (control.nacp)\n"
// runtime/game_info.c:54
#define SF2C_GAME_INFO_S_GAME_ORIGINAL_CONTROL_METADA_67B2 "[game] original control metadata unavailable; no display version claimed\n"
// runtime/game_info.c:56
#define SF2C_GAME_INFO_S_ICON_PNG_267C "icon.png"
// runtime/game_info.c:67
#define SF2C_GAME_INFO_S_U_U_U_C_F5C2 "%u.%u.%u%c"
// runtime/game_info.c:68
#define SF2C_GAME_INFO_N_0X3FF_6900 0x3ff
// runtime/game_info.c:68
#define SF2C_GAME_INFO_N_0XFFF_905E 0xfff
// runtime/game_info.c:69
#define SF2C_GAME_INFO_N_22_785F 22
// runtime/game_info.c:69
#define SF2C_GAME_INFO_N_12_6B51 12
// runtime/gamelib.c:8
#define SF2C_GAMELIB_N_32_E29C 32
// runtime/gamelib.c:16
#define SF2C_GAMELIB_S_MAIN_E4A9 "main"
// runtime/gamelib.c:16
#define SF2C_GAMELIB_S_SDK_DE0B "sdk"
// runtime/gamelib.c:19
#define SF2C_GAMELIB_N_4096_8B92 4096
// runtime/gamelib.c:20
#define SF2C_GAMELIB_N_0X60_3D6E 0x60
// runtime/gamelib.c:21
#define SF2C_GAMELIB_S_S_EXEFS_S_3F4F "%s/exefs/%s"
// runtime/gamelib.c:22
#define SF2C_GAMELIB_S_GAME_DIRECTORY_PATH_IS_TOO_L_427C "game directory path is too long"
// runtime/gamelib.c:25
#define SF2C_GAMELIB_S_RB_46CA "rb"
// runtime/gamelib.c:26
#define SF2C_GAMELIB_S_NSO0_F632 "NSO0"
// runtime/gamelib.c:29
#define SF2C_GAMELIB_S_EXEFS_S_IS_MISSING_OR_IS_NOT_1290 "exefs/%s is missing or is not a valid NSO"
// runtime/gamelib.c:32
#define SF2C_GAMELIB_N_0X40_1AF3 0x40
// runtime/gamelib.c:33
#define SF2C_GAMELIB_S_EXEFS_S_DOES_NOT_MATCH_THIS__AA5D "exefs/%s does not match this executable's translated build; "
// runtime/gamelib.c:34
#define SF2C_GAMELIB_S_REGENERATE_AND_REBUILD_FROM__59C1 "regenerate and rebuild from these game files"
// runtime/gamelib.h:10
#define SF2C_GAMELIB_S_C_C2E8 "C"
// runtime/hle.h:18
#define SF2C_HLE_S_HLE_IMPLS_3066 "hle_impls"
// runtime/hle.h:20
#define SF2C_HLE_S_HLE_M_FE1A ".hle$m"
// runtime/hle.h:38
#define SF2C_HLE_S_RST_M_30FC ".rst$m"
// runtime/input_bind.h:10
#define SF2C_INPUT_BIND_S_C_C2E8 "C"
// runtime/input_bind.h:26
#define SF2C_INPUT_BIND_N_26_5F9C 26
// runtime/ipc.c:10
#define SF2C_IPC_S_SECTION_START_DATA_SERVICES_C3F3 "section$start$__DATA$__services"
// runtime/ipc.c:11
#define SF2C_IPC_S_SECTION_END_DATA_SERVICES_AEF2 "section$end$__DATA$__services"
// runtime/ipc.c:16
#define SF2C_IPC_S_SVC_A_F31B ".svc$a"
// runtime/ipc.c:17
#define SF2C_IPC_S_SVC_Z_11E0 ".svc$z"
// runtime/ipc.c:21
#define SF2C_IPC_S_IPC_STUB_S_CMD_U_IN_U_BYTES__48C6 "[ipc] STUB %s cmd %u (in %u bytes, x%d a%d b%d c%d) -> ok\n"
// runtime/ipc.c:24
#define SF2C_IPC_N_0X40_1AF3 0x40
// runtime/ipc.c:42
#define SF2C_IPC_S_EMPTY_12AE ""
// runtime/ipc.c:42
#define SF2C_IPC_S_R_E649 "+r"
// runtime/ipc.c:45
#define SF2C_IPC_S_IPC_NO_HLE_IMPLEMENTATION_FO_AFCB "[ipc] no HLE implementation for service '%s', using logging stub\n"
// runtime/ipc.c:64
#define SF2C_IPC_S_SM_042D "sm:"
// runtime/ipc.c:65
#define SF2C_IPC_S_IPC_CONNECTTONAMEDPORT_S_UNS_9E3F "[ipc] ConnectToNamedPort('%s') unsupported\n"
// runtime/ipc.c:78
#define SF2C_IPC_S_SM_GETSERVICE_S_N_AD81 "[sm] GetService %s\n"
// runtime/ipc.c:83
#define SF2C_IPC_S_SM_UNKNOWN_CMD_U_N_3D72 "[sm] unknown cmd %u\n"
// runtime/ipc.c:89
#define SF2C_IPC_N_28_59E1 28
// runtime/ipc.c:89
#define SF2C_IPC_N_32_E29C 32
// runtime/ipc.c:89
#define SF2C_IPC_N_36_76A5 36
// runtime/ipc.c:95
#define SF2C_IPC_S_IPC_SENDSYNCREQUEST_ON_BAD_H_19BB "[ipc] SendSyncRequest on bad handle %x\n"
// runtime/ipc.c:96
#define SF2C_IPC_N_0XE401_004F 0xE401
// runtime/ipc.c:102
#define SF2C_IPC_N_0XFFFF_F51F 0xFFFF
// runtime/ipc.c:103
#define SF2C_IPC_N_16_B17E 16
// runtime/ipc.c:103
#define SF2C_IPC_N_0XF_7419 0xF
// runtime/ipc.c:103
#define SF2C_IPC_N_20_F5CA 20
// runtime/ipc.c:103
#define SF2C_IPC_N_24_C235 24
// runtime/ipc.c:104
#define SF2C_IPC_N_0X3FF_5488 0x3FF
// runtime/ipc.c:105
#define SF2C_IPC_N_10_4A44 10
// runtime/ipc.c:107
#define SF2C_IPC_N_0X80000000U_AE05 0x80000000u
// runtime/ipc.c:116
#define SF2C_IPC_N_12_6B51 12
// runtime/ipc.c:116
#define SF2C_IPC_N_0X7_ECB4 0x7
// runtime/ipc.c:132
#define SF2C_IPC_N_15_E629 15
// runtime/ipc.c:144
#define SF2C_IPC_S_IPC_CLOSE_SESSION_S_HANDLE_X_51E1 "[ipc] close session %s (handle %x)\n"
// runtime/ipc.c:158
#define SF2C_IPC_N_256_51E8 256
// runtime/ipc.c:169
#define SF2C_IPC_S_SFCI_7A30 "SFCI"
// runtime/ipc.c:170
#define SF2C_IPC_S_IPC_BAD_SFCI_MAGIC_ON_S_DOMA_7BAE "[ipc] bad SFCI magic on %s (domain=%d):"
// runtime/ipc.c:171
#define SF2C_IPC_S_08X_981A " %08x"
// runtime/ipc.c:172
#define SF2C_IPC_S_N_D7A5 "\n"
// runtime/ipc.c:180
#define SF2C_IPC_S_IPC_DOMAIN_OBJECT_U_MISSING__BCA0 "[ipc] domain object %u missing\n"
// runtime/ipc.c:188
#define SF2C_IPC_S_IPC_CONTROL_U_ON_S_HANDLE_X__5A72 "[ipc] control %u on %s (handle %x)\n"
// runtime/ipc.c:203
#define SF2C_IPC_N_0X8000_7702 0x8000
// runtime/ipc.c:207
#define SF2C_IPC_S_IPC_CONTROL_CMD_U_N_4BF4 "[ipc] control cmd %u\n"
// runtime/ipc.c:210
#define SF2C_IPC_S_IPC_MESSAGE_TYPE_U_N_6DEB "[ipc] message type %u\n"
// runtime/ipc.c:228
#define SF2C_IPC_N_0X100_BE6E 0x100
// runtime/ipc.c:247
#define SF2C_IPC_S_SFCO_A1D4 "SFCO"
// runtime/ipc.h:28
#define SF2C_IPC_N_0X200_3790 0x200
// runtime/ipc.h:83
#define SF2C_IPC_S_SVC_M_CE49 ".svc$m"
// runtime/kernel.c:43
#define SF2C_KERNEL_N_0X100_BE6E 0x100
// runtime/kernel.c:46
#define SF2C_KERNEL_S_OUT_OF_HANDLES_N_81F8 "out of handles\n"
// runtime/kernel.c:51
#define SF2C_KERNEL_N_0X101_83A4 0x101
// runtime/kernel.c:86
#define SF2C_KERNEL_S_LARECOMP_TICK_SCALE_2FD4 "LARECOMP_TICK_SCALE"
// runtime/kernel.c:88
#define SF2C_KERNEL_N_192_EB3B 192
// runtime/kernel.c:88
#define SF2C_KERNEL_N_10000_39E5 10000
// runtime/kernel.c:93
#define SF2C_KERNEL_N_1000000000_52A5 1000000000
// runtime/kernel.c:116
#define SF2C_KERNEL_N_1024_E39E 1024
// runtime/kernel.c:125
#define SF2C_KERNEL_N_0XB_CCDE 0xB
// runtime/kernel.c:125
#define SF2C_KERNEL_N_0XC_D882 0xC
// runtime/kernel.c:132
#define SF2C_KERNEL_S_TOO_MANY_REGIONS_N_01E9 "too many regions\n"
// runtime/kernel.c:159
#define SF2C_KERNEL_N_0X28_EAB9 0x28
// runtime/kernel.c:171
#define SF2C_KERNEL_N_1ULL_622E 1ull
// runtime/kernel.c:171
#define SF2C_KERNEL_N_39_0B91 39
// runtime/kernel.c:176
#define SF2C_KERNEL_N_0X50_4171 0x50
// runtime/kernel.c:206
#define SF2C_KERNEL_S_EXIT_MAIN_GUEST_THREAD_LLU_E_9D56 "[exit] MAIN guest thread %llu entry %llx returns %llx pc %llx lr %llx\n"
// runtime/kernel.c:208
#define SF2C_KERNEL_N_30_624B 30
// runtime/kernel.c:212
#define SF2C_KERNEL_S_EXITDUMP_09C4 "[exitdump]"
// runtime/kernel.c:213
#define SF2C_KERNEL_N_29_3513 29
// runtime/kernel.c:214
#define SF2C_KERNEL_N_10_4A44 10
// runtime/kernel.c:214
#define SF2C_KERNEL_N_15_E629 15
// runtime/kernel.c:219
#define SF2C_KERNEL_S_EXIT_FRAME_D_LR_LLX_S_0X_LLX_DBB7 "[exit]   frame %d: lr %llx (%s + 0x%llx)\n"
// runtime/kernel.c:219
#define SF2C_KERNEL_S_EMPTY_15C5 "?"
// runtime/kernel.c:241
#define SF2C_KERNEL_N_15ULL_40A4 15ull
// runtime/kernel.c:242
#define SF2C_KERNEL_N_0ULL_DF1B 0ull
// runtime/kernel.c:247
#define SF2C_KERNEL_N_0X200_3790 0x200
// runtime/kernel.c:257
#define SF2C_KERNEL_N_256U_DE07 256u
// runtime/kernel.c:257
#define SF2C_KERNEL_N_20_F5CA 20
// runtime/kernel.c:258
#define SF2C_KERNEL_S_COULD_NOT_CREATE_A_GAME_THRE_AA69 "Could not create a game thread.\n"
// runtime/kernel.c:263
#define SF2C_KERNEL_N_256ULL_3175 256ull
// runtime/kernel.c:271
#define SF2C_KERNEL_N_0XFFFF_F51F 0xFFFF
// runtime/kernel.c:271
#define SF2C_KERNEL_N_0XFFFFULL_2BD3 0xFFFFull
// runtime/kernel.c:289
#define SF2C_KERNEL_S_LARECOMP_HANG_DUMP_B06A "LARECOMP_HANG_DUMP"
// runtime/kernel.c:389
#define SF2C_KERNEL_N_256_51E8 256
// runtime/kernel.c:395
#define SF2C_KERNEL_S_LARECOMP_KTRACE_A502 "LARECOMP_KTRACE"
// runtime/kernel.c:405
#define SF2C_KERNEL_N_16_B17E 16
// runtime/kernel.c:443
#define SF2C_KERNEL_S_LARECOMP_DEBUG_CV_LATE_WAIT_BD4A "LARECOMP_DEBUG_CV_LATE_WAIT"
// runtime/kernel.c:455
#define SF2C_KERNEL_N_0XFFFFFFFFU_7BAF 0xffffffffu
// runtime/kernel.c:580
#define SF2C_KERNEL_S_LARECOMP_VSYNC_BT_9A1D "LARECOMP_VSYNC_BT"
// runtime/kernel.c:586
#define SF2C_KERNEL_S_VSYNC_BT_WAIT_ON_VSYNC_D_HAN_880D "[vsync-bt] wait on vsync (%d handles, timeout %lld) lr %llx:\n"
// runtime/kernel.c:618
#define SF2C_KERNEL_S_GUEST_S_E008 "[guest] %.*s"
// runtime/kernel.c:619
#define SF2C_KERNEL_CH_N_AD65 '\n'
// runtime/kernel.c:627
#define SF2C_KERNEL_N_32_E29C 32
// runtime/kernel.c:627
#define SF2C_KERNEL_N_0X80_D41C 0x80
// runtime/kernel.c:638
#define SF2C_KERNEL_S_LARECOMP_PROF_SVC_6FEB "LARECOMP_PROF_SVC"
// runtime/kernel.c:641
#define SF2C_KERNEL_N_0X18_D47C 0x18
// runtime/kernel.c:646
#define SF2C_KERNEL_N_80_4844 80
// runtime/kernel.c:646
#define SF2C_KERNEL_N_0X34_806E 0x34
// runtime/kernel.c:646
#define SF2C_KERNEL_N_10000000_FA95 10000000
// runtime/kernel.c:646
#define SF2C_KERNEL_N_0X0B_7C21 0x0b
// runtime/kernel.c:646
#define SF2C_KERNEL_N_0X1C_7BDE 0x1c
// runtime/kernel.c:646
#define SF2C_KERNEL_N_20000_876C 20000
// runtime/kernel.c:648
#define SF2C_KERNEL_S_SLOW_T_LLU_SVC_02X_LLUMS_A_L_6B68 "[slow] t%llu svc%02x %llums a=%llx,%llx,%llx,%llx h0=%x lr=%llx\n"
// runtime/kernel.c:649
#define SF2C_KERNEL_N_1000000_6CCE 1000000
// runtime/kernel.c:651
#define SF2C_KERNEL_N_31_EB1E 31
// runtime/kernel.c:656
#define SF2C_KERNEL_N_2000000000ULL_C95E 2000000000ull
// runtime/kernel.c:662
#define SF2C_KERNEL_S_SVCPROF_T_D_EB5C "[svcprof] t%d"
// runtime/kernel.c:663
#define SF2C_KERNEL_S_02X_LLU_LLUMS_4D1D " %02x:%llu/%llums"
// runtime/kernel.c:666
#define SF2C_KERNEL_S_N_D7A5 "\n"
// runtime/kernel.c:671
#define SF2C_KERNEL_S_LARECOMP_TRACE_SVC_4E28 "LARECOMP_TRACE_SVC"
// runtime/kernel.c:675
#define SF2C_KERNEL_S_SVC_02X_LLX_LLX_LLX_LLX_LLX__80CD "[svc] %02x(%llx, %llx, %llx, %llx) -> %llx %llx %llx  @%llx\n"
// runtime/kernel.c:682
#define SF2C_KERNEL_N_0X01_B51C 0x01
// runtime/kernel.c:684
#define SF2C_KERNEL_S_KERNEL_SETHEAPSIZE_SIZE_LLX__91EC "[kernel] SetHeapSize size=%llx (max=%llx)\n"
// runtime/kernel.c:700
#define SF2C_KERNEL_N_0X02_E6BD 0x02
// runtime/kernel.c:700
#define SF2C_KERNEL_N_0X03_9D1B 0x03
// runtime/kernel.c:701
#define SF2C_KERNEL_N_0X04_B03A 0x04
// runtime/kernel.c:706
#define SF2C_KERNEL_N_0X05_4ED3 0x05
// runtime/kernel.c:711
#define SF2C_KERNEL_N_0X06_9AE3 0x06
// runtime/kernel.c:716
#define SF2C_KERNEL_N_0X07_A022 0x07
// runtime/kernel.c:716
#define SF2C_KERNEL_S_EXITPROCESS_N_C20C "ExitProcess\n"
// runtime/kernel.c:717
#define SF2C_KERNEL_N_0X08_1EC8 0x08
// runtime/kernel.c:720
#define SF2C_KERNEL_S_KERNEL_CREATETHREAD_ENTRY_LL_2B07 "[kernel] CreateThread entry=%llx stack=%llx prio=%d -> %x\n"
// runtime/kernel.c:725
#define SF2C_KERNEL_N_0X09_59B7 0x09
// runtime/kernel.c:732
#define SF2C_KERNEL_N_0X0A_AF2D 0x0A
// runtime/kernel.c:733
#define SF2C_KERNEL_N_0X0B_C106 0x0B
// runtime/kernel.c:737
#define SF2C_KERNEL_N_50000000_C546 50000000
// runtime/kernel.c:744
#define SF2C_KERNEL_N_0X0C_49AA 0x0C
// runtime/kernel.c:750
#define SF2C_KERNEL_N_0X0D_106A 0x0D
// runtime/kernel.c:756
#define SF2C_KERNEL_N_0X0E_D851 0x0E
// runtime/kernel.c:760
#define SF2C_KERNEL_N_0XF_7419 0xF
// runtime/kernel.c:763
#define SF2C_KERNEL_N_0X0F_1F1D 0x0F
// runtime/kernel.c:764
#define SF2C_KERNEL_N_0X10_BF9C 0x10
// runtime/kernel.c:765
#define SF2C_KERNEL_N_0X11_A3D6 0x11
// runtime/kernel.c:765
#define SF2C_KERNEL_N_0X12_9AE3 0x12
// runtime/kernel.c:765
#define SF2C_KERNEL_N_0X17_A99E 0x17
// runtime/kernel.c:782
#define SF2C_KERNEL_N_0X13_3ABB 0x13
// runtime/kernel.c:793
#define SF2C_KERNEL_N_0X14_F942 0x14
// runtime/kernel.c:794
#define SF2C_KERNEL_N_0X15_5FB5 0x15
// runtime/kernel.c:803
#define SF2C_KERNEL_N_0X16_D7D4 0x16
// runtime/kernel.c:815
#define SF2C_KERNEL_N_0X19_6E00 0x19
// runtime/kernel.c:826
#define SF2C_KERNEL_N_0X1A_7631 0x1A
// runtime/kernel.c:827
#define SF2C_KERNEL_N_0X1B_E0FA 0x1B
// runtime/kernel.c:828
#define SF2C_KERNEL_N_0X1C_E409 0x1C
// runtime/kernel.c:829
#define SF2C_KERNEL_N_0X1D_A216 0x1D
// runtime/kernel.c:830
#define SF2C_KERNEL_N_0X1E_43F5 0x1E
// runtime/kernel.c:831
#define SF2C_KERNEL_N_0X1F_A571 0x1F
// runtime/kernel.c:837
#define SF2C_KERNEL_N_0X21_A102 0x21
// runtime/kernel.c:838
#define SF2C_KERNEL_N_0X22_9F8A 0x22
// runtime/kernel.c:845
#define SF2C_KERNEL_N_0X24_EB21 0x24
// runtime/kernel.c:845
#define SF2C_KERNEL_N_0X81_C29D 0x81
// runtime/kernel.c:846
#define SF2C_KERNEL_N_0X25_2611 0x25
// runtime/kernel.c:852
#define SF2C_KERNEL_N_0X26_50D1 0x26
// runtime/kernel.c:853
#define SF2C_KERNEL_N_0X80000000U_AE05 0x80000000u
// runtime/kernel.c:853
#define SF2C_KERNEL_S_KERNEL_BREAK_NOTIFICATION_LL_6778 "[kernel] Break notification %llx\n"
// runtime/kernel.c:854
#define SF2C_KERNEL_S_SVCBREAK_REASON_LLX_INFO_LLX_C1F2 "svcBreak(reason=%llx, info=%llx, size=%llx) at %llx\n"
// runtime/kernel.c:855
#define SF2C_KERNEL_N_0X27_D7EC 0x27
// runtime/kernel.c:856
#define SF2C_KERNEL_N_0X29_F941 0x29
// runtime/kernel.c:862
#define SF2C_KERNEL_N_0XFFFFFFFFFFFFFFFFULL_D876 0xFFFFFFFFFFFFFFFFull
// runtime/kernel.c:867
#define SF2C_KERNEL_N_21_6F4B 21
// runtime/kernel.c:867
#define SF2C_KERNEL_N_0XCD500000ULL_1FE5 0xCD500000ull
// runtime/kernel.c:868
#define SF2C_KERNEL_N_22_785F 22
// runtime/kernel.c:868
#define SF2C_KERNEL_N_0X4000000_329B 0x4000000
// runtime/kernel.c:871
#define SF2C_KERNEL_N_11_4FC8 11
// runtime/kernel.c:872
#define SF2C_KERNEL_N_12_6B51 12
// runtime/kernel.c:873
#define SF2C_KERNEL_N_13_3FDB 13
// runtime/kernel.c:874
#define SF2C_KERNEL_N_14_8527 14
// runtime/kernel.c:876
#define SF2C_KERNEL_N_17_4523 17
// runtime/kernel.c:877
#define SF2C_KERNEL_N_18_4EC9 18
// runtime/kernel.c:877
#define SF2C_KERNEL_N_0X01006BB00C6F0000ULL_15CA 0x01006BB00C6F0000ull
// runtime/kernel.c:879
#define SF2C_KERNEL_N_24_C235 24
// runtime/kernel.c:887
#define SF2C_KERNEL_N_96_7B1A 96
// runtime/kernel.c:890
#define SF2C_KERNEL_S_KERNEL_GETINFO_UNKNOWN_ID_U__552E "[kernel] GetInfo unknown id %u/%llu\n"
// runtime/kernel.c:896
#define SF2C_KERNEL_N_0X2C_8A63 0x2C
// runtime/kernel.c:898
#define SF2C_KERNEL_N_0X7_ECB4 0x7
// runtime/kernel.c:901
#define SF2C_KERNEL_N_0X2D_A7B5 0x2D
// runtime/kernel.c:905
#define SF2C_KERNEL_N_0X32_8B2B 0x32
// runtime/kernel.c:908
#define SF2C_KERNEL_N_0X33_8FFB 0x33
// runtime/kernel.c:922
#define SF2C_KERNEL_N_33_C6F3 33
// runtime/kernel.c:923
#define SF2C_KERNEL_N_0X110_8C83 0x110
// runtime/kernel.c:924
#define SF2C_KERNEL_N_0X318_7E03 0x318
// runtime/kernel.c:930
#define SF2C_KERNEL_N_0X35_B555 0x35
// runtime/kernel.c:931
#define SF2C_KERNEL_N_0X45_17A0 0x45
// runtime/kernel.c:937
#define SF2C_KERNEL_S_EVT_GUEST_W_LLX_R_LLX_LR_LLX_57D0 "[evt] guest w=%llx r=%llx lr=%llx\n"
// runtime/kernel.c:941
#define SF2C_KERNEL_S_UNIMPLEMENTED_SVC_0X_X_AT_LL_FAF2 "unimplemented svc 0x%x at %llx (x0=%llx x1=%llx x2=%llx)\n"
// runtime/kernel.c:958
#define SF2C_KERNEL_N_3000000000ULL_16F2 3000000000ull
// runtime/kernel.c:960
#define SF2C_KERNEL_S_REBOOT_STILL_WAITING_FOR_D_G_6E07 "[reboot] still waiting for %d guest threads after %llums:\n"
// runtime/kernel.c:963
#define SF2C_KERNEL_S_REBOOT_THREAD_LLU_ENTRY_LLX__6491 "[reboot]   thread %llu (entry %llx) pc %llx lr %llx wait %d sync %d\n"
// runtime/kernel.c:968
#define SF2C_KERNEL_N_2000_81A8 2000
// runtime/kernel.c:974
#define SF2C_KERNEL_S_COULD_NOT_JOIN_A_GAME_THREAD_598A "Could not join a game thread.\n"
// runtime/kernel.c:980
#define SF2C_KERNEL_S_REBOOT_D_GUEST_THREADS_STOPP_A056 "[reboot] %d guest threads stopped in %llums\n"
// runtime/kernel.c:1037
#define SF2C_KERNEL_S_SPY_MODULE_BASE_LLX_S_N_8879 "[spy] module base %llx (%s)\n"
// runtime/kernel.c:1055
#define SF2C_KERNEL_S_S_SPY_T_LLU_HOST_PTHREAD_DEA_ABE4 "%s [spy] t%llu host pthread dead\n"
// runtime/kernel.c:1058
#define SF2C_KERNEL_N_60_39FA 60
// runtime/kernel.c:1058
#define SF2C_KERNEL_N_1000_4051 1000
// runtime/kernel.c:1060
#define SF2C_KERNEL_S_S_SPY_T_LLU_UNRESPONSIVE_60__4E8A "%s [spy] t%llu unresponsive 60 ms (blocked where signals do not reach)\n"
// runtime/kernel.c:1069
#define SF2C_KERNEL_S_S_SPY_T_LLU_RIP_LLX_LLX_RSP__E2EC "%s [spy] t%llu rip %llx (+%llx) rsp %llx rbp %llx\n"
// runtime/kernel.c:1071
#define SF2C_KERNEL_N_0X2000000ULL_5DDE 0x2000000ull
// runtime/kernel.c:1077
#define SF2C_KERNEL_S_S_SPY_T_LLU_RIP_IN_S_0X_LLX__EA37 "%s [spy] t%llu rip in %s +0x%llx (sym %s+0x%llx)\n"
// runtime/kernel.c:1080
#define SF2C_KERNEL_S_EMPTY_0933 "-"
// runtime/kernel.c:1086
#define SF2C_KERNEL_S_S_SPY_T_LLU_FRAME_D_RBP_CHAI_A705 "%s [spy] t%llu frame %d: rbp chain unreadable at %llx (frame pointers omitted?)\n"
// runtime/kernel.c:1097
#define SF2C_KERNEL_S_S_SPY_T_LLU_FRAME_D_RET_LLX__B2BE "%s [spy] t%llu frame %d ret %llx%s%s%s%llx\n"
// runtime/kernel.c:1098
#define SF2C_KERNEL_S_IN_0F80 " in "
// runtime/kernel.c:1098
#define SF2C_KERNEL_S_EMPTY_12AE ""
// runtime/kernel.c:1098
#define SF2C_KERNEL_S_0X_67BF " +0x"
// runtime/kernel.c:1110
#define SF2C_KERNEL_N_100_AD57 100
// runtime/kernel.c:1111
#define SF2C_KERNEL_S_S_G_KLOCK_HELD_FOR_100_MS_RE_F713 "%s g_klock held for 100+ ms: read without it\n"
// runtime/kernel.c:1112
#define SF2C_KERNEL_S_MUTEX_58C4 "mutex"
// runtime/kernel.c:1112
#define SF2C_KERNEL_S_CONDVAR_BB44 "condvar"
// runtime/kernel.c:1112
#define SF2C_KERNEL_S_ARBITER_E55E "arbiter"
// runtime/kernel.c:1118
#define SF2C_KERNEL_N_0X7003BB7A54ULL_222D 0x7003bb7a54ull
// runtime/kernel.c:1119
#define SF2C_KERNEL_N_0X7003BB7A4CULL_F5EA 0x7003bb7a4cull
// runtime/kernel.c:1120
#define SF2C_KERNEL_S_S_LOAD_MACHINE_LOADPHASE_U_S_B643 "%s load-machine: loadphase %u (%s) playerloop-guard %u\n"
// runtime/kernel.c:1121
#define SF2C_KERNEL_S_LOADING_NNMAIN_SPINS_NO_PLAY_C2F3 "loading (nnMain spins, no PlayerLoop)"
// runtime/kernel.c:1122
#define SF2C_KERNEL_S_CLEARED_PLAYERLOOP_RUNS_FRAM_E499 "cleared (PlayerLoop runs frames)"
// runtime/kernel.c:1122
#define SF2C_KERNEL_S_OTHER_D448 "other"
// runtime/kernel.c:1127
#define SF2C_KERNEL_S_S_THREAD_LLU_H_X_S_S_S_SKIP__04E0 "%s thread %llu h %x %s%s%s SKIP (started %d finished %d)\n"
// runtime/kernel.c:1128
#define SF2C_KERNEL_S_EMPTY_2C8B "("
// runtime/kernel.c:1128
#define SF2C_KERNEL_S_EMPTY_C921 ")"
// runtime/kernel.c:1130
#define SF2C_KERNEL_S_S_THREAD_LLU_H_X_PRIO_D_CORE_E419 "%s thread %llu h %x prio %d core %d entry %llx pc %llx%s%s%s lr %llx wait %s addr %llx mutex %llx tag %x%s%s for %llu ms\n"
// runtime/kernel.c:1132
#define SF2C_KERNEL_S_EMPTY_AB96 " ("
// runtime/kernel.c:1135
#define SF2C_KERNEL_S_IN_WAITSYNCHRONIZATION_654C " in WaitSynchronization"
// runtime/kernel.c:1135
#define SF2C_KERNEL_S_CANCEL_PENDING_C87D " (cancel pending)"
// runtime/kernel.c:1139
#define SF2C_KERNEL_S_S_MUTEX_WORD_X_S_N_A5D9 "%s   mutex word %x%s\n"
// runtime/kernel.c:1140
#define SF2C_KERNEL_S_WAITING_TO_BE_HANDED_IT_4D2A " (waiting to be handed it)"
// runtime/kernel.c:1145
#define SF2C_KERNEL_S_S_HANDLE_X_TYPE_D_S_N_E6F4 "%s   handle %x type %d%s\n"
// runtime/kernel.c:1145
#define SF2C_KERNEL_S_SIGNALED_9C12 " signaled"
// runtime/kernel.c:1149
#define SF2C_KERNEL_S_S_FRAME_D_LR_LLX_N_942D "%s   frame %d: lr %llx\n"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_CV_WAIT_539D "cv-wait"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_CV_WAIT_END_815A "cv-wait-end"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_CV_SIGNAL_BA59 "cv-signal"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_ARB_LOCK_F915 "arb-lock"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_ARB_LOCK_END_55E5 "arb-lock-end"
// runtime/kernel.c:1162
#define SF2C_KERNEL_S_ARB_UNLOCK_8C47 "arb-unlock"
// runtime/kernel.c:1164
#define SF2C_KERNEL_S_S_KERNEL_TRACE_LAST_LLU_OF_L_0689 "%s kernel trace, last %llu of %llu (ms before now; words before -> after):\n"
// runtime/kernel.c:1170
#define SF2C_KERNEL_S_S_LLD_T_LLU_CV_WAIT_KEY_LLX__648D "%s  -%lld t%llu cv-wait key %llx (%x) mutex %llx %x -> %x pc %llx lr %llx\n"
// runtime/kernel.c:1173
#define SF2C_KERNEL_S_S_LLD_T_LLU_CV_WAIT_END_KEY__FCED "%s  -%lld t%llu cv-wait-end key %llx (%x) mutex %llx (%x) r %x\n"
// runtime/kernel.c:1176
#define SF2C_KERNEL_S_S_LLD_T_LLU_CV_SIGNAL_KEY_LL_3D47 "%s  -%lld t%llu cv-signal key %llx %x -> %x count %d woke %u pc %llx lr %llx\n"
// runtime/kernel.c:1179
#define SF2C_KERNEL_S_S_LLD_T_LLU_S_MUTEX_LLX_X_X__2126 "%s  -%lld t%llu %s mutex %llx %x -> %x owner %x tag/r %x\n"
// runtime/kernel.c:1207
#define SF2C_KERNEL_N_0XFFFFFFFF_8755 0xffffffff
// runtime/kernel.c:1214
#define SF2C_KERNEL_S_PHASE_LOADPHASE_U_PLAYERLOOP_DA5E "[phase] loadphase %u playerloop-guard %u\n"
// runtime/kernel.c:1224
#define SF2C_KERNEL_N_1000000000ULL_E860 1000000000ull
// runtime/kernel.c:1227
#define SF2C_KERNEL_S_MAIN_GUEST_THREAD_IN_ONE_KER_4C73 "main guest thread in one kernel wait for %llu s"
// runtime/kernel.c:1243
#define SF2C_KERNEL_S_KERNEL_TRACE_QUIET_FOR_LLU_S_946D "kernel trace quiet for %llu s (main free-runs, no syscalls)"
// runtime/kernel.c:1249
#define SF2C_KERNEL_CH_EMPTY_CF57 ';'
// runtime/kernel.c:1249
#define SF2C_KERNEL_CH_EMPTY_730D ' '
// runtime/kernel.c:1253
#define SF2C_KERNEL_S_HANG_S_N_314F "[hang] ===== %s =====\n"
// runtime/kernel.c:1255
#define SF2C_KERNEL_S_HANG_A2CC "[hang]"
// runtime/kernel.c:1257
#define SF2C_KERNEL_S_HANG_N_72A0 "[hang] =====\n"
// runtime/kernel.c:1304
#define SF2C_KERNEL_S_EVT_PAIR_W_X_R_X_GUEST_PC_LL_43FE "[evt] pair w=%x r=%x guest pc %llx lr %llx tid %llu\n"
// runtime/lan.c:45
#define SF2C_LAN_S_DIFFERENT_SF2RECOMP_TOOL_VER_9665 "Different SF2Recomp tool versions. Install the same release on both devices."
// runtime/lan.c:57
#define SF2C_LAN_N_16_B17E 16
// runtime/lan.c:60
#define SF2C_LAN_N_256_51E8 256
// runtime/lan.c:73
#define SF2C_LAN_N_24_C235 24
// runtime/lan.c:73
#define SF2C_LAN_N_127_922C 127
// runtime/lan.c:93
#define SF2C_LAN_N_160_A512 160
// runtime/lan.c:102
#define SF2C_LAN_S_LAN_S_N_D37B "[lan] %s\n"
// runtime/lan.c:104
#define SF2C_LAN_S_S_0030 "%s"
// runtime/lan.c:110
#define SF2C_LAN_N_10_4A44 10
// runtime/lan.c:111
#define SF2C_LAN_CH_EMPTY_01F8 '-'
// runtime/lan.c:111
#define SF2C_LAN_CH_EMPTY_3604 '+'
// runtime/lan.c:111
#define SF2C_LAN_S_INVALID_LAN_NUMERIC_OPTION_FF5D "invalid LAN numeric option"
// runtime/lan.c:119
#define SF2C_LAN_S_CANNOT_MAKE_SOCKET_NONBLOCKI_BB3A "cannot make socket nonblocking"
// runtime/lan.c:124
#define SF2C_LAN_N_1000000000ULL_E860 1000000000ull
// runtime/lan.c:127
#define SF2C_LAN_S_SESSION_CANCELLED_4346 "session cancelled"
// runtime/lan.c:128
#define SF2C_LAN_S_PEER_TIMED_OUT_COMBAT_STOPPE_368C "peer timed out; combat stopped"
// runtime/lan.c:129
#define SF2C_LAN_N_1000_4051 1000
// runtime/lan.c:129
#define SF2C_LAN_N_100000_3BB7 100000
// runtime/lan.c:130
#define SF2C_LAN_N_1000000_6CCE 1000000
// runtime/lan.c:134
#define SF2C_LAN_S_SOCKET_WAIT_FAILED_B780 "socket wait failed"
// runtime/lan.c:148
#define SF2C_LAN_S_PEER_DISCONNECTED_COMBAT_STO_2C08 "peer disconnected; combat stopped"
// runtime/lan.c:149
#define SF2C_LAN_S_CONNECTION_FAILED_COMBAT_STO_453E "connection failed; combat stopped"
// runtime/lan.c:155
#define SF2C_LAN_S_TOO_MANY_INPUT_EVENTS_IN_ONE_EBE3 "too many input events in one step"
// runtime/lan.c:157
#define SF2C_LAN_N_12_6B51 12
// runtime/lan.c:158
#define SF2C_LAN_N_20_F5CA 20
// runtime/lan.c:158
#define SF2C_LAN_N_32_E29C 32
// runtime/lan.c:158
#define SF2C_LAN_N_28_59E1 28
// runtime/lan.c:164
#define SF2C_LAN_S_DIFFERENT_SF2RECOMP_LAN_PROT_C428 "Different SF2Recomp LAN protocols. Update both devices."
// runtime/lan.c:166
#define SF2C_LAN_S_INCOMPATIBLE_PEER_PACKET_TYP_67D2 "incompatible peer, packet type, role or step sequence"
// runtime/lan.c:167
#define SF2C_LAN_S_OVERSIZED_PEER_PACKET_86F1 "oversized peer packet"
// runtime/lan.c:170
#define SF2C_LAN_S_LAN_DIVERGENCE_AT_STEP_U_LOC_2CD2 "[lan] divergence at step %u: local %016"
// runtime/lan.c:170
#define SF2C_LAN_S_REMOTE_016_C567 " remote %016"
// runtime/lan.c:170
#define SF2C_LAN_S_N_D7A5 "\n"
// runtime/lan.c:171
#define SF2C_LAN_S_COMBAT_STATE_SETUP_MISMATCH__A291 "combat state/setup mismatch; refusing to continue"
// runtime/lan.c:177
#define SF2C_LAN_S_SF2_LAN_HOST_36C9 "SF2_LAN_HOST"
// runtime/lan.c:177
#define SF2C_LAN_S_SF2_LAN_JOIN_0B27 "SF2_LAN_JOIN"
// runtime/lan.c:178
#define SF2C_LAN_S_SF2_LAN_PORT_8363 "SF2_LAN_PORT"
// runtime/lan.c:178
#define SF2C_LAN_S_SF2_LAN_SEED_9C02 "SF2_LAN_SEED"
// runtime/lan.c:179
#define SF2C_LAN_S_SF2_LAN_MATCH_23F8 "SF2_LAN_MATCH"
// runtime/lan.c:179
#define SF2C_LAN_S_SF2_LAN_DELAY_71B5 "SF2_LAN_DELAY"
// runtime/lan.c:181
#define SF2C_LAN_S_LAN_OPTIONS_REQUIRE_HOST_OR__504D "LAN options require HOST or JOIN"
// runtime/lan.c:184
#define SF2C_LAN_S_CHOOSE_ONE_LAN_ROLE_RESTART__5160 "choose one LAN role; restart to reconnect"
// runtime/lan.c:185
#define SF2C_LAN_S_THE_HOST_CHOOSES_THE_MATCH_S_3A57 "the host chooses the match, seed and input delay"
// runtime/lan.c:187
#define SF2C_LAN_N_65535_F2F8 65535
// runtime/lan.c:187
#define SF2C_LAN_N_34250_67EB 34250
// runtime/lan.c:187
#define SF2C_LAN_S_PORT_MUST_BE_POSITIVE_8198 "port must be positive"
// runtime/lan.c:188
#define SF2C_LAN_N_17_4523 17
// runtime/lan.c:188
#define SF2C_LAN_N_18_4EC9 18
// runtime/lan.c:188
#define SF2C_LAN_N_13_3FDB 13
// runtime/lan.c:188
#define SF2C_LAN_S_SF2_REPLAY_TICKS_1950 "SF2_REPLAY_TICKS"
// runtime/lan.c:189
#define SF2C_LAN_S_STEP_LIMIT_MUST_BE_POSITIVE_CAD0 "step limit must be positive"
// runtime/lan.c:191
#define SF2C_LAN_N_64_A68B 64
// runtime/lan.c:191
#define SF2C_LAN_S_INVALID_MATCH_SELECTION_013C "invalid match selection"
// runtime/lan.c:193
#define SF2C_LAN_CH_EMPTY_EE1B ','
// runtime/lan.c:193
#define SF2C_LAN_S_MATCH_REQUIRES_FIGHTER_FIGHT_CFE1 "MATCH requires fighter,fighter,arena indices"
// runtime/lan.c:193
#define SF2C_LAN_N_255_9556 255
// runtime/lan.c:196
#define SF2C_LAN_S_CANNOT_INITIALIZE_WINDOWS_SO_842A "cannot initialize Windows sockets"
// runtime/lan.c:200
#define SF2C_LAN_S_USE_A_NUMERIC_IPV4_ADDRESS_5296 "use a numeric IPv4 address"
// runtime/lan.c:202
#define SF2C_LAN_S_CANNOT_CREATE_LISTENER_EA33 "cannot create listener"
// runtime/lan.c:206
#define SF2C_LAN_S_CANNOT_LISTEN_ON_REQUESTED_A_2E30 "cannot listen on requested address/port"
// runtime/lan.c:207
#define SF2C_LAN_S_LAN_HOSTING_ON_S_U_WAITING_F_79AA "[lan] hosting on %s:%u; waiting for player 2\n"
// runtime/lan.c:208
#define SF2C_LAN_N_120_2ABA 120
// runtime/lan.c:210
#define SF2C_LAN_S_CANNOT_ACCEPT_PEER_6A1A "cannot accept peer"
// runtime/lan.c:212
#define SF2C_LAN_S_LAN_CONNECTING_TO_S_U_AS_PLA_549C "[lan] connecting to %s:%u as player 2\n"
// runtime/lan.c:215
#define SF2C_LAN_S_CANNOT_CREATE_CONNECTION_D2C2 "cannot create connection"
// runtime/lan.c:227
#define SF2C_LAN_S_HOST_CONNECTION_TIMED_OUT_9468 "host connection timed out"
// runtime/lan.c:227
#define SF2C_LAN_N_100_AD57 100
// runtime/lan.c:234
#define SF2C_LAN_N_15_E629 15
// runtime/lan.c:240
#define SF2C_LAN_S_PEER_REJECTED_MATCH_CONFIGUR_86C0 "peer rejected match configuration"
// runtime/lan.c:242
#define SF2C_LAN_S_UNSUPPORTED_GAME_REVISION_CO_B6F3 "unsupported game revision/configuration"
// runtime/lan.c:243
#define SF2C_LAN_S_INVALID_PEER_ROSTER_INDEX_D911 "invalid peer roster index"
// runtime/lan.c:244
#define SF2C_LAN_S_INVALID_PEER_INPUT_DELAY_FEEB "invalid peer input delay"
// runtime/lan.c:246
#define SF2C_LAN_S_INVALID_PEER_STEP_LIMIT_35B5 "invalid peer step limit"
// runtime/lan.c:247
#define SF2C_LAN_S_HOST_JOIN_DIAGNOSTIC_STEP_LI_B204 "host/join diagnostic step limits differ"
// runtime/lan.c:254
#define SF2C_LAN_S_LAN_CONNECTED_PLAYER_U_SEED__938E "[lan] connected: player %u, seed %u, match %u,%u,%u, delay %u steps\n"
// runtime/lan.c:265
#define SF2C_LAN_S_LAN_LOCAL_LOADING_COMPLETE_W_08AD "[lan] local loading complete; waiting for other player\n"
// runtime/lan.c:265
#define SF2C_LAN_S_LAN_COMBAT_PREPARED_WAITING__1D9D "[lan] combat prepared; waiting for peer readiness\n"
// runtime/lan.c:267
#define SF2C_LAN_S_PEER_DID_NOT_BECOME_READY_4864 "peer did not become ready"
// runtime/lan.c:276
#define SF2C_LAN_S_PEER_DISCONNECTED_DURING_REA_9B87 "peer disconnected during readiness"
// runtime/lan.c:288
#define SF2C_LAN_S_INITIAL_COMBAT_STATE_SETUP_M_D8D5 "initial combat state/setup mismatch"
// runtime/lan.c:290
#define SF2C_LAN_S_LAN_BOTH_PLAYERS_LOADED_STAR_8EB8 "[lan] both players loaded; starting fight intro\n"
// runtime/lan.c:290
#define SF2C_LAN_S_LAN_BOTH_FIGHTERS_READY_STAR_5A12 "[lan] both fighters ready; starting synchronized combat\n"
// runtime/lan.c:298
#define SF2C_LAN_S_PEER_CHECKPOINT_OUTSIDE_THE__05DB "peer checkpoint outside the input window"
// runtime/lan.c:301
#define SF2C_LAN_S_MALFORMED_INPUT_BATCH_AB5F "malformed input batch"
// runtime/lan.c:305
#define SF2C_LAN_N_14_8527 14
// runtime/lan.c:305
#define SF2C_LAN_S_PEER_SENT_INVALID_FIGHTER_IN_E7D8 "peer sent invalid fighter input"
// runtime/lan.c:313
#define SF2C_LAN_S_INVALID_LOCAL_STEP_INPUT_BAT_ECB5 "invalid local step/input batch"
// runtime/lan.c:319
#define SF2C_LAN_S_INVALID_LOCAL_INPUT_5806 "invalid local input"
// runtime/lan.c:337
#define SF2C_LAN_N_600_284B 600
// runtime/lan.c:337
#define SF2C_LAN_S_LAN_SYNCHRONIZED_STEP_U_HASH_2788 "[lan] synchronized step %u, hash %016"
// runtime/lan.c:337
#define SF2C_LAN_S_SENT_0A6E ", sent %"
// runtime/lan.c:337
#define SF2C_LAN_S_RECEIVED_3B36 " received %"
// runtime/lan.c:337
#define SF2C_LAN_S_BYTES_N_95EE " bytes\n"
// runtime/lan.c:345
#define SF2C_LAN_S_INVALID_SESSION_END_C96E "invalid session end"
// runtime/lan.c:346
#define SF2C_LAN_S_LAN_COMPLETED_U_SYNCHRONIZED_6A2E "[lan] completed %u synchronized combat steps; sent %"
// runtime/lan.c:360
#define SF2C_LAN_N_128_2747 128
// runtime/lan.c:394
#define SF2C_LAN_S_CANNOT_INITIALIZE_LOCAL_NETW_46B2 "Cannot initialize local networking."
// runtime/lan.c:405
#define SF2C_LAN_S_CANNOT_OPEN_LAN_DISCOVERY_74B2 "Cannot open LAN discovery."
// runtime/lan.c:409
#define SF2C_LAN_S_LAN_DISCOVERY_PORT_34251_IS__8676 "LAN discovery port 34251 is already in use."
// runtime/lan.c:416
#define SF2C_LAN_S_CANNOT_OPEN_A_LAN_HOST_044F "Cannot open a LAN host."
// runtime/lan.c:421
#define SF2C_LAN_S_CANNOT_HOST_TCP_PORT_34250_I_2FA2 "Cannot host: TCP port 34250 is already in use."
// runtime/lan.c:426
#define SF2C_LAN_S_LAN_IN_GAME_HOST_READY_ON_TC_DDE8 "[lan] in-game host ready on TCP %u; discovery UDP %u\n"
// runtime/lan.c:436
#define SF2C_LAN_N_5000000000ULL_3F32 5000000000ull
// runtime/lan.c:441
#define SF2C_LAN_S_CANNOT_OPEN_THE_HOST_CONNECT_75BF "Cannot open the host connection."
// runtime/lan.c:444
#define SF2C_LAN_S_INVALID_DISCOVERED_HOST_ADDR_D31A "Invalid discovered host address."
// runtime/lan.c:446
#define SF2C_LAN_S_THE_HOST_IS_NO_LONGER_AVAILA_3B98 "The host is no longer available. Refresh the host list."
// runtime/lan.c:452
#define SF2C_LAN_S_TOO_MANY_PENDING_SELECTION_I_1725 "Too many pending selection inputs."
// runtime/lan.c:457
#define SF2C_LAN_S_THE_OTHER_PLAYER_LEFT_SPARRI_75E0 "The other player left Sparring."
// runtime/lan.c:458
#define SF2C_LAN_S_THE_LAN_CONNECTION_WAS_LOST_5E98 "The LAN connection was lost."
// runtime/lan.c:461
#define SF2C_LAN_S_THE_OTHER_GAME_USES_AN_INCOM_F5EC "The other game uses an incompatible LAN protocol."
// runtime/lan.c:472
#define SF2C_LAN_N_96_7B1A 96
// runtime/lan.c:490
#define SF2C_LAN_S_LAN_DISCOVERY_FAILED_1A41 "LAN discovery failed."
// runtime/lan.c:495
#define SF2C_LAN_N_63_DA4E 63
// runtime/lan.c:495
#define SF2C_LAN_S_SHADOW_FIGHT_2_5A40 "Shadow Fight 2"
// runtime/lan.c:497
#define SF2C_LAN_N_83_BBB9 83
// runtime/lan.c:497
#define SF2C_LAN_N_126_65A6 126
// runtime/lan.c:497
#define SF2C_LAN_CH_EMPTY_D13B '<'
// runtime/lan.c:497
#define SF2C_LAN_CH_EMPTY_4DE9 '>'
// runtime/lan.c:497
#define SF2C_LAN_CH_EMPTY_BDC4 '_'
// runtime/lan.c:498
#define SF2C_LAN_N_84_44C8 84
// runtime/lan.c:498
#define SF2C_LAN_N_88_8B94 88
// runtime/lan.c:499
#define SF2C_LAN_N_92_8241 92
// runtime/lan.c:506
#define SF2C_LAN_S_127_0_0_1_8827 "127.0.0.1"
// runtime/lan.c:523
#define SF2C_LAN_S_CANNOT_ACCEPT_A_LAN_PLAYER_4682 "Cannot accept a LAN player."
// runtime/lan.c:535
#define SF2C_LAN_S_THE_HOST_CONNECTION_WAS_REFU_D327 "The host connection was refused."
// runtime/lan.c:544
#define SF2C_LAN_S_CONNECTION_TIMED_OUT_TRY_JOI_82AA "Connection timed out. Try joining again."
// runtime/lan.c:552
#define SF2C_LAN_S_DIFFERENT_GAME_LAN_VERSIONS__2164 "Different game/LAN versions. Update both games."
// runtime/lan.c:563
#define SF2C_LAN_S_INCOMPATIBLE_MATCH_CONFIGURA_F2E2 "Incompatible match configuration."
// runtime/lan.c:564
#define SF2C_LAN_S_INVALID_ROSTER_INDEX_72CE "Invalid roster index."
// runtime/lan.c:565
#define SF2C_LAN_S_THE_GUEST_REJECTED_THE_MATCH_8A8E "The guest rejected the match settings."
// runtime/lan.c:568
#define SF2C_LAN_S_LAN_IN_GAME_CONNECTION_ESTAB_BCE9 "[lan] in-game connection established as player %u\n"
// runtime/lan.c:573
#define SF2C_LAN_S_INVALID_SELECTED_FIGHTER_OR__9257 "Invalid selected fighter or arena."
// runtime/lan.c:575
#define SF2C_LAN_S_INVALID_LAN_LOBBY_PACKET_EF22 "Invalid LAN lobby packet."
// runtime/lan.c:583
#define SF2C_LAN_S_INVALID_LOCAL_MENU_INPUT_4D85 "Invalid local menu input."
// runtime/lan.c:598
#define SF2C_LAN_S_UNSUPPORTED_SELECTED_ROSTER__55D6 "Unsupported selected roster index."
// runtime/lan.c:613
#define SF2C_LAN_S_THE_OTHER_PLAYER_LEFT_WHILE__CC0E "The other player left while loading combat."
// runtime/lan.c:614
#define SF2C_LAN_S_CONNECTION_LOST_WHILE_LOADIN_36BE "Connection lost while loading combat."
// runtime/lan.c:619
#define SF2C_LAN_S_INVALID_SELECTION_PACKET_AT__EB53 "Invalid selection packet at the combat boundary."
// runtime/lan_random.h:7
#define SF2C_LAN_RANDOM_N_30_624B 30
// runtime/lan_random.h:7
#define SF2C_LAN_RANDOM_N_UINT64_C_0XBF58476D1CE4E5B9_B50B UINT64_C(0xbf58476d1ce4e5b9)
// runtime/lan_random.h:8
#define SF2C_LAN_RANDOM_N_27_6706 27
// runtime/lan_random.h:8
#define SF2C_LAN_RANDOM_N_UINT64_C_0X94D049BB133111EB_95E5 UINT64_C(0x94d049bb133111eb)
// runtime/lan_random.h:9
#define SF2C_LAN_RANDOM_N_31_EB1E 31
// runtime/lan_random.h:12
#define SF2C_LAN_RANDOM_N_UINT64_C_0X9E3779B97F4A7C15_B09C UINT64_C(0x9e3779b97f4a7c15)
// runtime/loader.c:11
#define SF2C_LOADER_N_23_535F 23
// runtime/loader.c:11
#define SF2C_LOADER_N_25_B7A5 25
// runtime/loader.c:11
#define SF2C_LOADER_N_26_5F9C 26
// runtime/loader.c:11
#define SF2C_LOADER_N_27_6706 27
// runtime/loader.c:11
#define SF2C_LOADER_N_28_59E1 28
// runtime/loader.c:12
#define SF2C_LOADER_N_257_4C97 257
// runtime/loader.c:12
#define SF2C_LOADER_N_1025_4637 1025
// runtime/loader.c:12
#define SF2C_LOADER_N_1026_582C 1026
// runtime/loader.c:12
#define SF2C_LOADER_N_1027_60F2 1027
// runtime/loader.c:23
#define SF2C_LOADER_N_15_E629 15
// runtime/loader.c:23
#define SF2C_LOADER_N_255_9556 255
// runtime/loader.c:41
#define SF2C_LOADER_S_RB_46CA "rb"
// runtime/loader.c:55
#define SF2C_LOADER_S_NSO0_F632 "NSO0"
// runtime/loader.c:55
#define SF2C_LOADER_S_CANNOT_LOAD_S_N_E740 "cannot load %s\n"
// runtime/loader.c:56
#define SF2C_LOADER_N_0XC_D882 0xC
// runtime/loader.c:58
#define SF2C_LOADER_N_0X10_BF9C 0x10
// runtime/loader.c:58
#define SF2C_LOADER_N_12_6B51 12
// runtime/loader.c:59
#define SF2C_LOADER_N_0X3C_E42C 0x3C
// runtime/loader.c:60
#define SF2C_LOADER_N_0X3FFF_F000 0x3FFF
// runtime/loader.c:60
#define SF2C_LOADER_N_0X3FFFULL_DDD1 0x3FFFull
// runtime/loader.c:63
#define SF2C_LOADER_N_0X60_3D6E 0x60
// runtime/loader.c:67
#define SF2C_LOADER_S_LZ4_S_SEG_D_ZU_U_N_AF48 "lz4 %s seg %d: %zu != %u\n"
// runtime/loader.c:88
#define SF2C_LOADER_N_13_3FDB 13
// runtime/loader.c:95
#define SF2C_LOADER_S_LOADED_5S_AT_LLX_SIZE_LLX_SY_DC29 "loaded %-5s at %llx size %llx syms %llu\n"
// runtime/loader.c:121
#define SF2C_LOADER_N_0XF_7419 0xF
// runtime/loader.c:137
#define SF2C_LOADER_S_LOADER_SYMBOL_S_FOUND_IN_GUE_AAD6 "[loader] symbol %s found in guest at %llx\n"
// runtime/loader.c:141
#define SF2C_LOADER_S_LOADER_RESOLVE_HLE_S_N_68FD "[loader] resolve HLE: %s\n"
// runtime/loader.c:146
#define SF2C_LOADER_S_LOADER_RESOLVE_UNRESOLVED_HL_0527 "[loader] resolve UNRESOLVED HLE stub: %s\n"
// runtime/loader.c:164
#define SF2C_LOADER_N_32_E29C 32
// runtime/loader.c:175
#define SF2C_LOADER_S_S_RELOC_TYPE_U_N_E2DA "%s: reloc type %u\n"
// runtime/loader.c:179
#define SF2C_LOADER_S_RELOCATED_5S_D_IMPORTS_BOUND_928E "relocated %-5s (%d imports bound to host stubs)\n"
// runtime/loader.c:183
#define SF2C_LOADER_N_1024_E39E 1024
// runtime/loader.c:184
#define SF2C_LOADER_S_MAIN_E4A9 "main"
// runtime/loader.c:184
#define SF2C_LOADER_S_SDK_DE0B "sdk"
// runtime/loader.c:188
#define SF2C_LOADER_S_S_S_22D8 "%s/%s"
// runtime/main.c:35
#define SF2C_MAIN_S_INIT_S_DT_INIT_LLX_LLU_INIT__E048 "init %s (DT_INIT %llx, %llu init_array entries)\n"
// runtime/main.c:49
#define SF2C_MAIN_N_101_16DC 101
// runtime/main.c:50
#define SF2C_MAIN_S_LARECOMP_REEXEC_FORCE_8398 "LARECOMP_REEXEC_FORCE"
// runtime/main.c:51
#define SF2C_MAIN_S_0_9808 "0"
// runtime/main.c:51
#define SF2C_MAIN_S_LARECOMP_REEXEC_1A16 "LARECOMP_REEXEC"
// runtime/main.c:52
#define SF2C_MAIN_S_LARECOMP_REEXEC_WHY_6D9D "LARECOMP_REEXEC_WHY"
// runtime/main.c:54
#define SF2C_MAIN_S_EMPTY_12AE ""
// runtime/main.c:57
#define SF2C_MAIN_N_20_F5CA 20
// runtime/main.c:58
#define SF2C_MAIN_S_FATAL_GUEST_ADDRESS_RANGE_LL_15E7 "FATAL: guest address range %llx-%llx is not free\n"
// runtime/main.c:61
#define SF2C_MAIN_N_16_B17E 16
// runtime/main.c:62
#define SF2C_MAIN_S_D_42B3 "%d"
// runtime/main.c:65
#define SF2C_MAIN_S_RE_EXEC_44E0 "re-exec"
// runtime/main.c:72
#define SF2C_MAIN_N_1024_E39E 1024
// runtime/main.c:73
#define SF2C_MAIN_S_S_EXEFS_2DFB "%s/exefs"
// runtime/main.c:74
#define SF2C_MAIN_S_S_MAIN_NPDM_A264 "%s/main.npdm"
// runtime/main.c:77
#define SF2C_MAIN_S_STARTUP_GUEST_MODULES_LOADED_53F3 "[startup] guest modules loaded in %llu ms\n"
// runtime/main.c:77
#define SF2C_MAIN_N_1000000_6CCE 1000000
// runtime/main.c:80
#define SF2C_MAIN_S_STARTUP_KERNEL_AND_DISPATCH__D685 "[startup] kernel and dispatch ready at %llu ms\n"
// runtime/main.c:83
#define SF2C_MAIN_N_0X100000_0A2A 0x100000
// runtime/main.c:84
#define SF2C_MAIN_N_44_71EE 44
// runtime/main.c:85
#define SF2C_MAIN_S_RB_46CA "rb"
// runtime/main.c:87
#define SF2C_MAIN_N_0X80_D41C 0x80
// runtime/main.c:88
#define SF2C_MAIN_S_META_0C27 "META"
// runtime/main.c:89
#define SF2C_MAIN_N_0XE_6A51 0xE
// runtime/main.c:90
#define SF2C_MAIN_N_0X1C_E409 0x1C
// runtime/main.c:94
#define SF2C_MAIN_S_MAIN_THREAD_PRIO_D_STACK_0X__7110 "main thread: prio %d, stack 0x%x\n"
// runtime/main.c:96
#define SF2C_MAIN_S_ZN2NN4INIT5STARTEMMPFVVES2_2B45 "_ZN2nn4init5StartEmmPFvvES2_"
// runtime/main.c:97
#define SF2C_MAIN_S_NN_INIT_START_NOT_FOUND_N_162D "nn::init::Start not found\n"
// runtime/main.c:98
#define SF2C_MAIN_S_RTLD_INIT_SDK_CAAC "<rtld_init_sdk>"
// runtime/main.c:99
#define SF2C_MAIN_S_RTLD_INIT_MAIN_C24D "<rtld_init_main>"
// runtime/main.c:115
#define SF2C_MAIN_S_INFO_SF2RECOMP_672D "[info] SF2Recomp "
// runtime/main.c:115
#define SF2C_MAIN_S_S_S_D_CORES_LLU_MB_N_BAEC ", %s, %s, %d cores, %llu MB\n"
// runtime/main.c:116
#define SF2C_MAIN_S_UNKNOWN_OS_DB5F "unknown OS"
// runtime/main.c:116
#define SF2C_MAIN_S_UNKNOWN_CPU_D8F2 "unknown CPU"
// runtime/main.c:121
#define SF2C_MAIN_S_VERSION_99AD "--version"
// runtime/main.c:122
#define SF2C_MAIN_S_SF2RECOMP_6431 "SF2Recomp "
// runtime/main.c:126
#define SF2C_MAIN_S_LARECOMP_DATA_ROOT_B22A "LARECOMP_DATA_ROOT"
// runtime/main.c:130
#define SF2C_MAIN_S_SF2_RECOMP_AA83 "SF2 Recomp"
// runtime/main.c:131
#define SF2C_MAIN_S_CANNOT_RESERVE_THE_GUEST_ADD_66A0 "Cannot reserve the guest address range. Close the game and launch it again."
// runtime/main.c:145
#define SF2C_MAIN_S_CHECK_GAME_C9D1 "--check-game"
// runtime/main.c:146
#define SF2C_MAIN_S_HELP_A603 "--help"
// runtime/main.c:147
#define SF2C_MAIN_S_USAGE_S_CHECK_GAME_EXTRACTED_0167 "Usage: %s [--check-game] [extracted-game-directory]\n"
// runtime/main.c:149
#define SF2C_MAIN_CH_EMPTY_01F8 '-'
// runtime/main.c:154
#define SF2C_MAIN_N_4096_8B92 4096
// runtime/main.c:157
#define SF2C_MAIN_CH_EMPTY_0825 '/'
// runtime/main.c:161
#define SF2C_MAIN_S_EXTRACTED_D952 "/extracted"
// runtime/main.c:166
#define SF2C_MAIN_S_CANNOT_OPEN_GAME_DIRECTORY_S_8A72 "Cannot open game directory %s: %s\n"
// runtime/main.c:168
#define SF2C_MAIN_S_SHADOW_FIGHT_2_5A40 "Shadow Fight 2"
// runtime/main.c:168
#define SF2C_MAIN_S_CANNOT_OPEN_THE_BUNDLED_GAME_20C8 "Cannot open the bundled game directory."
// runtime/main.c:173
#define SF2C_MAIN_N_512_94F8 512
// runtime/main.c:175
#define SF2C_MAIN_S_CANNOT_RUN_THESE_GAME_FILES__27D4 "Cannot run these game files: %s\n"
// runtime/main.c:182
#define SF2C_MAIN_S_GAME_FILES_MATCH_THE_TRANSLA_7408 "Game files match the translated main and SDK builds.\n"
// runtime/main.c:187
#define SF2C_MAIN_S_ROMFS_12A2 "/romfs"
// runtime/main.c:188
#define SF2C_MAIN_S_S_ROMFS_1B4C "%s/romfs"
// runtime/main.c:201
#define SF2C_MAIN_S_BOOT_CANNOT_START_BACKGROUND_A453 "[boot] cannot start background initialization: %s; loading on the main thread\n"
// runtime/main.c:207
#define SF2C_MAIN_S_BOOT_UI_RUN_RETURNED_N_8BF9 "[boot] ui_run returned\n"
// runtime/network.c:17
#define SF2C_NETWORK_S_BSD_7724 "bsd:"
// runtime/network.c:17
#define SF2C_NETWORK_S_NIFM_70EB "nifm:"
// runtime/network.c:17
#define SF2C_NETWORK_S_LDN_BEB9 "ldn:"
// runtime/network.c:17
#define SF2C_NETWORK_S_WLAN_33AE "wlan:"
// runtime/network.c:17
#define SF2C_NETWORK_S_ETHC_2BE1 "ethc:"
// runtime/network.c:17
#define SF2C_NETWORK_S_NSD_E9C9 "nsd:"
// runtime/network.c:17
#define SF2C_NETWORK_S_NTC_8088 "ntc:"
// runtime/network.c:21
#define SF2C_NETWORK_S_SSL_310B "ssl"
// runtime/network.c:21
#define SF2C_NETWORK_S_SFDNSRES_534C "sfdnsres"
// runtime/network.c:21
#define SF2C_NETWORK_S_NTC_B41F "ntc"
// runtime/network.c:29
#define SF2C_NETWORK_S_ZN2NN6SOCKET_644B "_ZN2nn6socket"
// runtime/network.c:29
#define SF2C_NETWORK_S_ZNK2NN6SOCKET_584A "_ZNK2nn6socket"
// runtime/network.c:29
#define SF2C_NETWORK_S_NNSOCKET_A5CB "nnsocket"
// runtime/network.c:30
#define SF2C_NETWORK_S_ZN2NN3SSL_726C "_ZN2nn3ssl"
// runtime/network.c:30
#define SF2C_NETWORK_S_ZNK2NN3SSL_8E05 "_ZNK2nn3ssl"
// runtime/network.c:30
#define SF2C_NETWORK_S_NNSSL_8A12 "nnssl"
// runtime/network.c:31
#define SF2C_NETWORK_S_ZN2NN4NIFM_A31F "_ZN2nn4nifm"
// runtime/network.c:31
#define SF2C_NETWORK_S_ZNK2NN4NIFM_3F29 "_ZNK2nn4nifm"
// runtime/network.c:32
#define SF2C_NETWORK_S_ZN2NN3LDN_704A "_ZN2nn3ldn"
// runtime/network.c:32
#define SF2C_NETWORK_S_ZNK2NN3LDN_56C7 "_ZNK2nn3ldn"
// runtime/network.c:44
#define SF2C_NETWORK_N_0XFA01_EA29 0xFA01
// runtime/network.c:55
#define SF2C_NETWORK_N_13_3FDB 13
// runtime/network.c:56
#define SF2C_NETWORK_N_0XC65B0_DEBC 0xc65b0
// runtime/network.c:65
#define SF2C_NETWORK_N_0XD6A80_29EF 0xd6a80
// runtime/paths.c:19
#define SF2C_PATHS_CH_EMPTY_0825 '/'
// runtime/paths.c:21
#define SF2C_PATHS_CH_EMPTY_4266 ':'
// runtime/paths.c:30
#define SF2C_PATHS_CH_EMPTY_6672 '\\'
// runtime/paths.c:33
#define SF2C_PATHS_S_S_0030 "%s"
// runtime/paths.c:35
#define SF2C_PATHS_S_S_S_22D8 "%s/%s"
// runtime/paths.c:56
#define SF2C_PATHS_N_0755_64A5 0755
// runtime/paths.c:66
#define SF2C_PATHS_S_LARECOMP_DATA_ROOT_B22A "LARECOMP_DATA_ROOT"
// runtime/paths.c:67
#define SF2C_PATHS_S_LARECOMP_DATA_HOME_1E74 "LARECOMP_DATA_HOME"
// runtime/paths.c:73
#define SF2C_PATHS_S_SF2_CONFIG_3707 "SF2_CONFIG"
// runtime/paths.c:77
#define SF2C_PATHS_S_S_SF2RECOMP_INI_76B3 "%s/sf2recomp.ini"
// runtime/paths.c:80
#define SF2C_PATHS_S_SF2_RECOMP_CANNOT_INITIALIZE_3A6E "SF2 Recomp cannot initialize its config/data directory: %s\n"
// runtime/paths.c:84
#define SF2C_PATHS_S_SF2_RECOMP_DATA_DIRECTORY_47C9 "SF2 Recomp data directory"
// runtime/paths.c:90
#define SF2C_PATHS_S_PATHS_DATA_S_N_3917 "[paths] data: %s\n"
// runtime/paths.h:9
#define SF2C_PATHS_S_C_C2E8 "C"
// runtime/platform.h:18
#define SF2C_PLATFORM_S_C_C2E8 "C"
// runtime/platform.h:39
#define SF2C_PLATFORM_N_0X4000ULL_E24A 0x4000ull
// runtime/platform.h:64
#define SF2C_PLATFORM_N_128_2747 128
// runtime/platform.h:66
#define SF2C_PLATFORM_N_48_9801 48
// runtime/platform.h:87
#define SF2C_PLATFORM_N_256_51E8 256
// runtime/platform.h:94
#define SF2C_PLATFORM_S_VENV_SCRIPTS_PYTHON_EXE_1A88 "venv/Scripts/python.exe"
// runtime/replay.c:15
#define SF2C_REPLAY_N_65536_0F25 65536
// runtime/replay.c:15
#define SF2C_REPLAY_N_4096_8B92 4096
// runtime/replay.c:31
#define SF2C_REPLAY_N_1024_E39E 1024
// runtime/replay.c:52
#define SF2C_REPLAY_S_REPLAY_S_N_03CA "[replay] %s\n"
// runtime/replay.c:59
#define SF2C_REPLAY_N_10_4A44 10
// runtime/replay.c:60
#define SF2C_REPLAY_CH_EMPTY_01F8 '-'
// runtime/replay.c:60
#define SF2C_REPLAY_CH_EMPTY_3604 '+'
// runtime/replay.c:60
#define SF2C_REPLAY_S_INVALID_NUMERIC_OPTION_9134 "invalid numeric option"
// runtime/replay.c:67
#define SF2C_REPLAY_S_SF2_REPLAY_TRACE_2CB0 "SF2_REPLAY_TRACE"
// runtime/replay.c:67
#define SF2C_REPLAY_S_SF2_REPLAY_INPUT_88E5 "SF2_REPLAY_INPUT"
// runtime/replay.c:68
#define SF2C_REPLAY_S_SF2_REPLAY_RECORD_FBB2 "SF2_REPLAY_RECORD"
// runtime/replay.c:68
#define SF2C_REPLAY_S_SF2_REPLAY_TICKS_1950 "SF2_REPLAY_TICKS"
// runtime/replay.c:69
#define SF2C_REPLAY_S_SF2_REPLAY_MATCH_ABA9 "SF2_REPLAY_MATCH"
// runtime/replay.c:70
#define SF2C_REPLAY_S_SF2_REPLAY_SEED_30B5 "SF2_REPLAY_SEED"
// runtime/replay.c:72
#define SF2C_REPLAY_S_SF2_REPLAY_TRACE_IS_REQUIRED_BA76 "SF2_REPLAY_TRACE is required for replay options"
// runtime/replay.c:75
#define SF2C_REPLAY_S_INPUT_AND_RECORD_ARE_MUTUALL_49CD "INPUT and RECORD are mutually exclusive"
// runtime/replay.c:76
#define SF2C_REPLAY_S_TRACE_AND_INPUT_RECORD_PATHS_B715 "trace and input/record paths must differ"
// runtime/replay.c:77
#define SF2C_REPLAY_N_1000000_6CCE 1000000
// runtime/replay.c:77
#define SF2C_REPLAY_N_600_284B 600
// runtime/replay.c:78
#define SF2C_REPLAY_S_SF2_REPLAY_TICKS_MUST_BE_POS_230F "SF2_REPLAY_TICKS must be positive"
// runtime/replay.c:82
#define SF2C_REPLAY_N_64_A68B 64
// runtime/replay.c:83
#define SF2C_REPLAY_S_INVALID_FIXED_MATCH_INDICES_59B0 "invalid fixed match indices"
// runtime/replay.c:87
#define SF2C_REPLAY_CH_EMPTY_EE1B ','
// runtime/replay.c:88
#define SF2C_REPLAY_S_MATCH_REQUIRES_CHARACTER_CHA_9327 "MATCH requires character,character,location indices"
// runtime/replay.c:90
#define SF2C_REPLAY_N_255_9556 255
// runtime/replay.c:96
#define SF2C_REPLAY_S_LAN_CHOOSES_INITIALIZATION_R_B1F1 "LAN chooses initialization; replay SEED/MATCH/RECORD are not allowed"
// runtime/replay.c:101
#define SF2C_REPLAY_S_R_2659 "r"
// runtime/replay.c:102
#define SF2C_REPLAY_S_CANNOT_OPEN_INPUT_TAPE_0566 "cannot open input tape"
// runtime/replay.c:103
#define SF2C_REPLAY_N_256_51E8 256
// runtime/replay.c:104
#define SF2C_REPLAY_S_SF2_REPLAY_1_N_2DC7 "SF2_REPLAY 1\n"
// runtime/replay.c:104
#define SF2C_REPLAY_S_INVALID_INPUT_TAPE_HEADER_2E89 "invalid input tape header"
// runtime/replay.c:106
#define SF2C_REPLAY_S_CANNOT_ALLOCATE_INPUT_TAPE_B5F9 "cannot allocate input tape"
// runtime/replay.c:110
#define SF2C_REPLAY_CH_EMPTY_4734 '#'
// runtime/replay.c:110
#define SF2C_REPLAY_CH_N_AD65 '\n'
// runtime/replay.c:113
#define SF2C_REPLAY_S_T_R_N_6308 " \t\r\n"
// runtime/replay.c:114
#define SF2C_REPLAY_S_INPUT_TAPE_REQUIRES_FOUR_UNS_40A1 "input tape requires four unsigned fields"
// runtime/replay.c:116
#define SF2C_REPLAY_N_14_8527 14
// runtime/replay.c:120
#define SF2C_REPLAY_S_REPLAY_INVALID_INPUT_TAPE_LI_326F "[replay] invalid input tape line %u\n"
// runtime/replay.c:125
#define SF2C_REPLAY_S_CANNOT_READ_INPUT_TAPE_D48C "cannot read input tape"
// runtime/replay.c:130
#define SF2C_REPLAY_S_CANNOT_CREATE_BOUNDED_STATE__7BA0 "cannot create bounded state-hash scratch stream"
// runtime/replay.c:132
#define SF2C_REPLAY_S_WX_6B8F "wx"
// runtime/replay.c:133
#define SF2C_REPLAY_S_CANNOT_CREATE_TRACE_OUTPUT_M_F6E9 "cannot create trace (output must not exist)"
// runtime/replay.c:136
#define SF2C_REPLAY_S_CANNOT_CREATE_RECORD_TAPE_OU_7DAC "cannot create record tape (output must not exist)"
// runtime/replay.c:141
#define SF2C_REPLAY_S_REPLAY_S_U_COMBAT_STEPS_S_N_243C "[replay] %s, %u combat steps, %s\n"
// runtime/replay.c:141
#define SF2C_REPLAY_S_PLAYBACK_67D7 "playback"
// runtime/replay.c:141
#define SF2C_REPLAY_S_OBSERVE_RECORD_8C4E "observe/record"
// runtime/replay.c:142
#define SF2C_REPLAY_S_CONTROLLED_INITIALIZATION_AN_2FFD "controlled initialization and scoped random streams"
// runtime/replay.c:142
#define SF2C_REPLAY_S_ORIGINAL_RANDOM_TIMING_BEHAV_87F9 "original random/timing behavior"
// runtime/replay.c:145
#define SF2C_REPLAY_S_KIND_HEADER_SCHEMA_1_GAME_UP_5A81 "{\"kind\":\"header\",\"schema\":1,\"game\":\"update_v131072\","
// runtime/replay.c:146
#define SF2C_REPLAY_S_LIMIT_U_PLAYBACK_S_EVENTS_ZU_1BDB "\"limit\":%u,\"playback\":%s,\"events\":%zu,\"fps\":%d,\"aspect\":%d,"
// runtime/replay.c:147
#define SF2C_REPLAY_S_RESOLUTION_D_UPSCALER_D_AA_D_7D52 "\"resolution\":%d,\"upscaler\":%d,\"aa\":%d,\"ai_enabled\":%s,"
// runtime/replay.c:148
#define SF2C_REPLAY_S_RNG_POLICY_S_SEED_U_INITIALI_55BA "\"rng_policy\":\"%s\",\"seed\":%u,\"initialization\":\"%s\",\"fixed_match\":%s,"
// runtime/replay.c:149
#define SF2C_REPLAY_S_COMBAT_GEOMETRY_PVP_FRAMING__D175 "\"combat_geometry\":\"pvp-framing-16:9-v2\",\"coverage\":\"observed-combat-fields-v3\",\"end_on_match\":%s}\n"
// runtime/replay.c:150
#define SF2C_REPLAY_S_TRUE_18D1 "true"
// runtime/replay.c:150
#define SF2C_REPLAY_S_FALSE_076D "false"
// runtime/replay.c:152
#define SF2C_REPLAY_S_SCOPED_CALLSITE_LCG_V2_14CF "scoped-callsite-lcg-v2"
// runtime/replay.c:152
#define SF2C_REPLAY_S_ORIGINAL_UNMODIFIED_952E "original-unmodified"
// runtime/replay.c:153
#define SF2C_REPLAY_S_LAN_TWO_HUMAN_NATIVE_STANCE__5D80 "lan-two-human-native-stance-240-v1"
// runtime/replay.c:153
#define SF2C_REPLAY_S_NATIVE_ROUND_STANCE_240_STEP_C958 "native-round-stance-240-steps-v1"
// runtime/replay.c:153
#define SF2C_REPLAY_S_ORIGINAL_4D60 "original"
// runtime/replay.c:165
#define SF2C_REPLAY_N_56_7688 56
// runtime/replay.c:165
#define SF2C_REPLAY_N_0XFFFFFFFFU_7BAF 0xffffffffu
// runtime/replay.c:169
#define SF2C_REPLAY_S_TOO_MANY_SCOPED_RANDOM_STREA_6AA7 "too many scoped random streams"
// runtime/replay.c:173
#define SF2C_REPLAY_N_32_E29C 32
// runtime/replay.c:173
#define SF2C_REPLAY_N_UINT64_C_0X534632524E475632_418C UINT64_C(0x534632524e475632)
// runtime/replay.c:173
#define SF2C_REPLAY_N_0X7FFFFFFFU_7A69 0x7fffffffu
// runtime/replay.c:177
#define SF2C_REPLAY_N_0X41C64E6DU_FE1E 0x41c64e6du
// runtime/replay.c:177
#define SF2C_REPLAY_N_0X3039U_2AC9 0x3039u
// runtime/replay.c:194
#define SF2C_REPLAY_N_0XC0_4E7B 0xc0
// runtime/replay.c:194
#define SF2C_REPLAY_N_0XB8_8F73 0xb8
// runtime/replay.c:194
#define SF2C_REPLAY_N_0X140_A49E 0x140
// runtime/replay.c:195
#define SF2C_REPLAY_S_LAN_FIGHTER_PARAMETERS_ARE_M_EDA9 "LAN fighter parameters are missing"
// runtime/replay.c:196
#define SF2C_REPLAY_N_0X62_88BC 0x62
// runtime/replay.c:196
#define SF2C_REPLAY_N_0X63_A6C6 0x63
// runtime/replay.c:201
#define SF2C_REPLAY_N_0X7000CA7E80ULL_C4A9 0x7000ca7e80ull
// runtime/replay.c:201
#define SF2C_REPLAY_N_0X30_8BAD 0x30
// runtime/replay.c:201
#define SF2C_REPLAY_N_0X18_D47C 0x18
// runtime/replay.c:201
#define SF2C_REPLAY_N_18_4EC9 18
// runtime/replay.c:202
#define SF2C_REPLAY_N_0X7000CA6DF0ULL_A60A 0x7000ca6df0ull
// runtime/replay.c:202
#define SF2C_REPLAY_N_0X7000CA7C00ULL_BAC6 0x7000ca7c00ull
// runtime/replay.c:204
#define SF2C_REPLAY_N_0X7000C9DD50ULL_1622 0x7000c9dd50ull
// runtime/replay.c:209
#define SF2C_REPLAY_N_0X1A0_98D8 0x1a0
// runtime/replay.c:209
#define SF2C_REPLAY_N_0X80_D41C 0x80
// runtime/replay.c:210
#define SF2C_REPLAY_N_0X70_B0F6 0x70
// runtime/replay.c:212
#define SF2C_REPLAY_N_0X24_EB21 0x24
// runtime/replay.c:212
#define SF2C_REPLAY_N_0X60_3D6E 0x60
// runtime/replay.c:213
#define SF2C_REPLAY_S_CONTROLLED_INITIALIZATION_MI_01A7 "controlled initialization missed the new fight boundary"
// runtime/replay.c:215
#define SF2C_REPLAY_N_0X7000CA7100ULL_DA44 0x7000ca7100ull
// runtime/replay.c:218
#define SF2C_REPLAY_S_REPLAY_INITIALIZED_NATIVE_RO_40D0 "[replay] initialized native round and stance; warming 240 combat steps\n"
// runtime/replay.c:220
#define SF2C_REPLAY_N_240_6AF1 240
// runtime/replay.c:223
#define SF2C_REPLAY_S_LAN_COMBAT_COULD_NOT_INITIAL_7E4B "LAN combat could not initialize"
// runtime/replay.c:228
#define SF2C_REPLAY_S_LAN_READY_BARRIER_PASSED_AT__60E6 "[lan] ready barrier passed at present %"
// runtime/replay.c:228
#define SF2C_REPLAY_S_N_D7A5 "\n"
// runtime/replay.c:236
#define SF2C_REPLAY_N_0X7000C99E30ULL_08A4 0x7000c99e30ull
// runtime/replay.c:242
#define SF2C_REPLAY_N_0X7001177D10ULL_B365 0x7001177d10ull
// runtime/replay.c:243
#define SF2C_REPLAY_N_0X10_BF9C 0x10
// runtime/replay.c:243
#define SF2C_REPLAY_N_0X14_F942 0x14
// runtime/replay.c:246
#define SF2C_REPLAY_N_0X7001177D20ULL_6E12 0x7001177d20ull
// runtime/replay.c:247
#define SF2C_REPLAY_N_30_624B 30
// runtime/replay.c:251
#define SF2C_REPLAY_N_0X7001237BC0ULL_48FE 0x7001237bc0ull
// runtime/replay.c:255
#define SF2C_REPLAY_N_31_EB1E 31
// runtime/replay.c:258
#define SF2C_REPLAY_N_0X7001237B50ULL_DDBF 0x7001237b50ull
// runtime/replay.c:260
#define SF2C_REPLAY_S_NONFINITE_CONTROLLED_UNITY_R_0847 "nonfinite controlled Unity random bounds"
// runtime/replay.c:261
#define SF2C_REPLAY_N_0XFFFFFFU_0F78 0xffffffu
// runtime/replay.c:261
#define SF2C_REPLAY_N_16777215_0F_EA68 16777215.0f
// runtime/replay.c:268
#define SF2C_REPLAY_N_0X70010ED6A0ULL_3FF9 0x70010ed6a0ull
// runtime/replay.c:268
#define SF2C_REPLAY_N_0X70010F1450ULL_9A8B 0x70010f1450ull
// runtime/replay.c:268
#define SF2C_REPLAY_N_0X70010F1F90ULL_C00E 0x70010f1f90ull
// runtime/replay.c:268
#define SF2C_REPLAY_N_0X70010F2DE0ULL_E9D4 0x70010f2de0ull
// runtime/replay.c:280
#define SF2C_REPLAY_S_INVALID_OVERSIZED_GUEST_LIST_21B6 "invalid/oversized guest list in combat snapshot"
// runtime/replay.c:283
#define SF2C_REPLAY_N_0X20_A766 0x20
// runtime/replay.c:285
#define SF2C_REPLAY_S_EMPTY_3251 '"'
// runtime/replay.c:287
#define SF2C_REPLAY_S_INVALID_GUEST_STRING_IN_COMB_0E00 "invalid guest string in combat snapshot"
// runtime/replay.c:290
#define SF2C_REPLAY_N_127_922C 127
// runtime/replay.c:290
#define SF2C_REPLAY_CH_EMPTY_6672 '\\'
// runtime/replay.c:291
#define SF2C_REPLAY_S_U_04X_B175 "\\u%04x"
// runtime/replay.c:314
#define SF2C_REPLAY_CH_EMPTY_854D '['
// runtime/replay.c:315
#define SF2C_REPLAY_S_S_U_35E0 "%s%u"
// runtime/replay.c:315
#define SF2C_REPLAY_S_EMPTY_08B5 ","
// runtime/replay.c:315
#define SF2C_REPLAY_S_EMPTY_12AE ""
// runtime/replay.c:316
#define SF2C_REPLAY_CH_EMPTY_CB80 ']'
// runtime/replay.c:332
#define SF2C_REPLAY_N_0XA0_3862 0xa0
// runtime/replay.c:337
#define SF2C_REPLAY_N_0X210_7579 0x210
// runtime/replay.c:337
#define SF2C_REPLAY_N_0X1D0_216A 0x1d0
// runtime/replay.c:338
#define SF2C_REPLAY_S_NAME_0B4D "{\"name\":"
// runtime/replay.c:338
#define SF2C_REPLAY_N_0X1B8_642F 0x1b8
// runtime/replay.c:339
#define SF2C_REPLAY_S_EQUIPMENT_F74C ",\"equipment\":"
// runtime/replay.c:340
#define SF2C_REPLAY_S_HEALTH_BITS_U_USER_U_AI_U_MO_654E ",\"health_bits\":%u,\"user\":%u,\"ai\":%u,\"model\":"
// runtime/replay.c:341
#define SF2C_REPLAY_N_0X7000FFBF90ULL_F3D5 0x7000ffbf90ull
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X118_23BF 0x118
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X130_CCDF 0x130
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X148_30C9 0x148
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X15C_13EF 0x15c
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X164_974D 0x164
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X168_35C9 0x168
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X170_9B54 0x170
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X174_7CDD 0x174
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X190_6770 0x190
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X194_CE4B 0x194
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X198_4F92 0x198
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X1E0_10BA 0x1e0
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X228_4737 0x228
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X230_E5E9 0x230
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X234_5C29 0x234
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X238_FF98 0x238
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X23C_5533 0x23c
// runtime/replay.c:342
#define SF2C_REPLAY_N_0X284_1C6F 0x284
// runtime/replay.c:344
#define SF2C_REPLAY_S_FLAGS_U_U_U_U_U_U_U_ANIMATIO_B764 ",\"flags\":[%u,%u,%u,%u,%u,%u,%u],\"animation\":"
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X158_924E 0x158
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X159_54D8 0x159
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X160_8A14 0x160
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X161_6CA8 0x161
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X22C_56FC 0x22c
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X22D_7853 0x22d
// runtime/replay.c:345
#define SF2C_REPLAY_N_0X241_C8E9 0x241
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X34_806E 0x34
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X38_AE4F 0x38
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X48_0EF0 0x48
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X4C_C1BA 0x4c
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X50_4171 0x50
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X54_9433 0x54
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X58_08FB 0x58
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X5C_F9CC 0x5c
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X64_3246 0x64
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X6C_3CD4 0x6c
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X74_D298 0x74
// runtime/replay.c:346
#define SF2C_REPLAY_N_0X78_756D 0x78
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XA8_D409 0xa8
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XAC_3360 0xac
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XB0_D785 0xb0
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XB4_F215 0xb4
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XC4_EDF1 0xc4
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XD8_8DD9 0xd8
// runtime/replay.c:346
#define SF2C_REPLAY_N_0XDC_2690 0xdc
// runtime/replay.c:348
#define SF2C_REPLAY_S_ANIMATION_NAME_5C41 ",\"animation_name\":"
// runtime/replay.c:348
#define SF2C_REPLAY_N_0XF0_2A69 0xf0
// runtime/replay.c:348
#define SF2C_REPLAY_N_0X40_1AF3 0x40
// runtime/replay.c:349
#define SF2C_REPLAY_S_SHIFT_A88D ",\"shift\":"
// runtime/replay.c:349
#define SF2C_REPLAY_N_0X88_AC70 0x88
// runtime/replay.c:350
#define SF2C_REPLAY_S_VELOCITY_5137 ",\"velocity\":"
// runtime/replay.c:350
#define SF2C_REPLAY_N_0X90_4255 0x90
// runtime/replay.c:351
#define SF2C_REPLAY_S_CONTROLLER_U_U_KEYS_444D ",\"controller\":[%u,%u],\"keys\":["
// runtime/replay.c:356
#define SF2C_REPLAY_S_S_U_U_U_746F "%s[%u,%u,%u]"
// runtime/replay.c:358
#define SF2C_REPLAY_S_KEY_DATA_9DAC "],\"key_data\":["
// runtime/replay.c:359
#define SF2C_REPLAY_N_0X28_EAB9 0x28
// runtime/replay.c:361
#define SF2C_REPLAY_S_NODES_826F "],\"nodes\":["
// runtime/replay.c:362
#define SF2C_REPLAY_N_0X1C8_EAA2 0x1c8
// runtime/replay.c:366
#define SF2C_REPLAY_S_S_ID_U_CURRENT_EB9E "%s{\"id\":%u,\"current\":"
// runtime/replay.c:366
#define SF2C_REPLAY_N_0X3C_4150 0x3c
// runtime/replay.c:367
#define SF2C_REPLAY_S_PREVIOUS_FB21 ",\"previous\":"
// runtime/replay.c:367
#define SF2C_REPLAY_CH_EMPTY_9AC4 '}'
// runtime/replay.c:369
#define SF2C_REPLAY_S_PHYSICS_FRAMES_U_9DB4 "],\"physics_frames\":%u}"
// runtime/replay.c:369
#define SF2C_REPLAY_N_0X200_3790 0x200
// runtime/replay.c:373
#define SF2C_REPLAY_N_UINT64_C_1099511628211_3F75 UINT64_C(1099511628211)
// runtime/replay.c:380
#define SF2C_REPLAY_S_KIND_STATE_TICK_U_STATE_STAG_91BB "{\"kind\":\"state\",\"tick\":%u,\"state\":{\"stage\":%u,\"fight_time\":%u,"
// runtime/replay.c:381
#define SF2C_REPLAY_S_ROUND_U_U_U_U_U_FLAGS_U_U_U__BF5C "\"round\":[%u,%u,%u,%u,%u],\"flags\":[%u,%u,%u,%u,%u],\"models\":["
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X1C_7BDE 0x1c
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X21_A102 0x21
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X91_BC34 0x91
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X92_E9B0 0x92
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X101_83A4 0x101
// runtime/replay.c:383
#define SF2C_REPLAY_N_0X155_338E 0x155
// runtime/replay.c:384
#define SF2C_REPLAY_N_0X84_7CD7 0x84
// runtime/replay.c:384
#define SF2C_REPLAY_N_0X7C_3CC4 0x7c
// runtime/replay.c:384
#define SF2C_REPLAY_N_0X8C_65B3 0x8c
// runtime/replay.c:387
#define SF2C_REPLAY_S_ARENA_FRAMING_FCA8 "],\"arena_framing\":"
// runtime/replay.c:389
#define SF2C_REPLAY_S_RANDOM_STREAMS_DFE6 ",\"random_streams\":["
// runtime/replay.c:390
#define SF2C_REPLAY_S_S_016_9810 "%s[\"%016"
// runtime/replay.c:390
#define SF2C_REPLAY_S_U_U_C6E2 "\",%u,%u]"
// runtime/replay.c:391
#define SF2C_REPLAY_S_EMPTY_21FB "]}"
// runtime/replay.c:393
#define SF2C_REPLAY_S_DIAGNOSTIC_PRESENT_EEDE ",\"diagnostic\":{\"present\":%"
// runtime/replay.c:393
#define SF2C_REPLAY_S_ELAPSED_NS_46D3 ",\"elapsed_ns\":%"
// runtime/replay.c:393
#define SF2C_REPLAY_S_FIGHT_FRAME_U_RELATIVE_FRAME_A46E ",\"fight_frame\":%u,\"relative_frame\":%u}}\n"
// runtime/replay.c:395
#define SF2C_REPLAY_S_CANNOT_WRITE_COMBAT_TRACE_57B8 "cannot write combat trace"
// runtime/replay.c:397
#define SF2C_REPLAY_S_CANNOT_MEASURE_STATE_HASH_19DF "cannot measure state hash"
// runtime/replay.c:401
#define SF2C_REPLAY_S_CANNOT_READ_STATE_HASH_B615 "cannot read state hash"
// runtime/replay.c:406
#define SF2C_REPLAY_S_CANNOT_WRITE_LAN_TRACE_773C "cannot write LAN trace"
// runtime/replay.c:413
#define SF2C_REPLAY_S_CANNOT_FLUSH_LAN_TRACE_3BFD "cannot flush LAN trace"
// runtime/replay.c:419
#define SF2C_REPLAY_S_KIND_COMPLETE_TICKS_U_EVENTS_3ADE "{\"kind\":\"complete\",\"ticks\":%u,\"events_applied\":%zu,\"reason\":\"%s\"}\n"
// runtime/replay.c:420
#define SF2C_REPLAY_S_CANNOT_FINISH_TRACE_4009 "cannot finish trace"
// runtime/replay.c:421
#define SF2C_REPLAY_S_CANNOT_FINISH_RECORDING_F370 "cannot finish recording"
// runtime/replay.c:422
#define SF2C_REPLAY_S_REPLAY_COMPLETED_U_COMBAT_ST_2483 "[replay] completed %u combat steps, %zu inputs applied\n"
// runtime/replay.c:429
#define SF2C_REPLAY_S_PVP_COMBAT_HAS_MISSING_FIGHT_5FD8 "PvP combat has missing fighters"
// runtime/replay.c:432
#define SF2C_REPLAY_S_MISSING_PVP_SELECTION_IDENTI_CA53 "missing PvP selection identity; cannot compare this match"
// runtime/replay.c:433
#define SF2C_REPLAY_S_KIND_SETUP_LOCATION_9381 "{\"kind\":\"setup\",\"location\":"
// runtime/replay.c:434
#define SF2C_REPLAY_S_PLAYERS_B46A ",\"players\":["
// runtime/replay.c:435
#define SF2C_REPLAY_S_LOCATION_TEMPLATE_48AC "],\"location_template\":"
// runtime/replay.c:436
#define SF2C_REPLAY_S_EQUIPMENT_E719 ",\"equipment\":["
// runtime/replay.c:436
#define SF2C_REPLAY_S_N_300B "]}\n"
// runtime/replay.c:437
#define SF2C_REPLAY_N_UINT64_C_1469598103934665603_4020 UINT64_C(14695981039346656037)
// runtime/replay.c:446
#define SF2C_REPLAY_S_REPLAY_FIRST_ACTIVE_PVP_COMB_37F1 "[replay] first active PvP combat step at present %"
// runtime/replay.c:455
#define SF2C_REPLAY_S_LAN_FIGHT_CHANGED_BEFORE_SES_7723 "LAN fight changed before session end"
// runtime/replay.c:458
#define SF2C_REPLAY_S_UNEXPECTED_LAN_ROUND_SEQUENC_3F56 "unexpected LAN round sequence"
// runtime/replay.c:462
#define SF2C_REPLAY_S_LAN_NATIVE_ROUND_U_SYNCHRONI_36F6 "[lan] native round %u; synchronized 240-step stance\n"
// runtime/replay.c:470
#define SF2C_REPLAY_S_FIGHT_CHANGED_BEFORE_REPLAY__C140 "fight changed before replay completed"
// runtime/replay.c:471
#define SF2C_REPLAY_S_NESTED_COMBAT_STEP_5BD3 "nested combat step"
// runtime/replay.c:478
#define SF2C_REPLAY_S_TOO_MANY_LOCAL_INPUTS_IN_ONE_C344 "too many local inputs in one step"
// runtime/replay.c:483
#define SF2C_REPLAY_N_0X7000FF7AF0ULL_9A66 0x7000ff7af0ull
// runtime/replay.c:483
#define SF2C_REPLAY_N_0X7000FF7C40ULL_F462 0x7000ff7c40ull
// runtime/replay.c:485
#define SF2C_REPLAY_S_KIND_INPUT_TICK_U_PLAYER_U_C_53D5 "{\"kind\":\"input\",\"tick\":%u,\"player\":%u,\"code\":%u,\"down\":%u}\n"
// runtime/replay.c:501
#define SF2C_REPLAY_N_0X7000DA027CULL_6732 0x7000da027cull
// runtime/replay.c:513
#define SF2C_REPLAY_S_UNKNOWN_LOCAL_FIGHTER_INPUT_DC46 "unknown local fighter input"
// runtime/replay.c:522
#define SF2C_REPLAY_N_0X7000D961F0ULL_34E0 0x7000d961f0ull
// runtime/replay.c:526
#define SF2C_REPLAY_N_0X7000CF9830ULL_7401 0x7000cf9830ull
// runtime/replay.c:531
#define SF2C_REPLAY_S_FIXED_MATCH_INDEX_IS_OUTSIDE_6D48 "fixed match index is outside the PvP roster"
// runtime/replay.c:533
#define SF2C_REPLAY_CH_R_5BDE 'r'
// runtime/replay.c:533
#define SF2C_REPLAY_CH_A_D749 'a'
// runtime/replay.c:533
#define SF2C_REPLAY_CH_N_76E4 'n'
// runtime/replay.c:533
#define SF2C_REPLAY_CH_D_8F5F 'd'
// runtime/replay.c:533
#define SF2C_REPLAY_CH_O_5820 'o'
// runtime/replay.c:533
#define SF2C_REPLAY_CH_M_4109 'm'
// runtime/replay.c:534
#define SF2C_REPLAY_S_FIXED_MATCH_CANNOT_SELECT_RA_8804 "fixed match cannot select Random"
// runtime/replay.c:541
#define SF2C_REPLAY_S_INVALID_PVP_SELECTION_IDENTI_798F "invalid PvP selection identity"
// runtime/replay.c:547
#define SF2C_REPLAY_N_0X7000C9DE18ULL_49F6 0x7000c9de18ull
// runtime/replay.c:547
#define SF2C_REPLAY_N_19_9400 19
// runtime/replay.c:548
#define SF2C_REPLAY_N_0X7000C9DE40ULL_6267 0x7000c9de40ull
// runtime/replay.c:555
#define SF2C_REPLAY_N_0X108_650A 0x108
// runtime/replay.c:557
#define SF2C_REPLAY_S_LAN_MATCH_FINISHED_AFTER_NAT_EBF6 "[lan] match finished after native round %u; winner player %u\n"
// runtime/replay.c:558
#define SF2C_REPLAY_S_MATCH_FINISHED_5F09 "match_finished"
// runtime/replay.c:562
#define SF2C_REPLAY_S_LIMIT_2D05 "limit"
// runtime/replay.c:568
#define SF2C_REPLAY_S_LIVE_INPUT_OCCURRED_WITHIN_A_E1A2 "live input occurred within a combat step; tape cannot preserve its ordering"
// runtime/replay.c:573
#define SF2C_REPLAY_S_U_U_U_U_N_0E23 "%u %u %u %u\n"
// runtime/replay.c:574
#define SF2C_REPLAY_S_CANNOT_WRITE_INPUT_TAPE_C520 "cannot write input tape"
// runtime/replay.c:596
#define SF2C_REPLAY_S_CANNOT_CREATE_COMBAT_CHECKPO_7D0D "cannot create combat checkpoint stream"
// runtime/replay.c:598
#define SF2C_REPLAY_S_LARECOMP_LAN_TRACE_DIR_236B "LARECOMP_LAN_TRACE_DIR"
// runtime/replay.c:601
#define SF2C_REPLAY_S_S_MATCH_U_JSONL_F685 "%s/match-%u.jsonl"
// runtime/replay.c:601
#define SF2C_REPLAY_S_TRACE_PATH_IS_TOO_LONG_DCE0 "trace path is too long"
// runtime/replay.c:602
#define SF2C_REPLAY_S_CANNOT_CREATE_NATIVE_LAN_DIA_16A2 "cannot create native LAN diagnostic trace"
// runtime/replay.h:5
#define SF2C_REPLAY_N_2U_8C70 2u
// runtime/romfs_build.c:45
#define SF2C_ROMFS_BUILD_N_32_E29C 32
// runtime/romfs_build.c:53
#define SF2C_ROMFS_BUILD_CH_EMPTY_277A '.'
// runtime/romfs_build.c:56
#define SF2C_ROMFS_BUILD_S_S_S_22D8 "%s/%s"
// runtime/romfs_build.c:68
#define SF2C_ROMFS_BUILD_N_16_B17E 16
// runtime/romfs_build.c:91
#define SF2C_ROMFS_BUILD_N_256_51E8 256
// runtime/romfs_build.c:95
#define SF2C_ROMFS_BUILD_N_64_A68B 64
// runtime/romfs_build.c:103
#define SF2C_ROMFS_BUILD_N_123456789U_6733 123456789u
// runtime/romfs_build.c:104
#define SF2C_ROMFS_BUILD_N_27_6706 27
// runtime/romfs_build.c:109
#define SF2C_ROMFS_BUILD_N_19_9400 19
// runtime/romfs_build.c:111
#define SF2C_ROMFS_BUILD_N_11_4FC8 11
// runtime/romfs_build.c:111
#define SF2C_ROMFS_BUILD_N_13_3FDB 13
// runtime/romfs_build.c:111
#define SF2C_ROMFS_BUILD_N_17_4523 17
// runtime/romfs_build.c:115
#define SF2C_ROMFS_BUILD_N_3U_3B80 3u
// runtime/romfs_build.c:116
#define SF2C_ROMFS_BUILD_N_15_E629 15
// runtime/romfs_build.c:116
#define SF2C_ROMFS_BUILD_N_15ULL_40A4 15ull
// runtime/romfs_build.c:121
#define SF2C_ROMFS_BUILD_S_EMPTY_12AE ""
// runtime/romfs_build.c:140
#define SF2C_ROMFS_BUILD_S_CAN_T_READ_S_BA37 "can't read %s"
// runtime/romfs_build.c:145
#define SF2C_ROMFS_BUILD_S_NO_FILES_IN_S_E978 "no files in %s"
// runtime/romfs_build.c:149
#define SF2C_ROMFS_BUILD_N_0X18_D47C 0x18
// runtime/romfs_build.c:150
#define SF2C_ROMFS_BUILD_N_0X20_A766 0x20
// runtime/romfs_build.c:176
#define SF2C_ROMFS_BUILD_N_12_6B51 12
// runtime/romfs_build.c:178
#define SF2C_ROMFS_BUILD_N_20_F5CA 20
// runtime/romfs_build.c:193
#define SF2C_ROMFS_BUILD_N_24_C235 24
// runtime/romfs_build.c:194
#define SF2C_ROMFS_BUILD_N_28_59E1 28
// runtime/romfs_build.c:200
#define SF2C_ROMFS_BUILD_N_3ULL_BD3E 3ull
// runtime/romfs_build.c:201
#define SF2C_ROMFS_BUILD_N_4ULL_C089 4ull
// runtime/romfs_build.c:202
#define SF2C_ROMFS_BUILD_N_10_4A44 10
// runtime/romfs_build.c:202
#define SF2C_ROMFS_BUILD_N_0X50_4171 0x50
// runtime/romfs_build.c:244
#define SF2C_ROMFS_BUILD_S_RB_46CA "rb"
// runtime/romfs_build.c:244
#define SF2C_ROMFS_BUILD_S_WB_3D6A "wb"
// runtime/romfs_build.c:265
#define SF2C_ROMFS_BUILD_N_0755_64A5 0755
// runtime/romfs_build.h:10
#define SF2C_ROMFS_BUILD_S_C_C2E8 "C"
// runtime/rt.h:14
#define SF2C_RT_N_2048_BFA0 2048
// runtime/rt.h:31
#define SF2C_RT_N_0X7000000000ULL_65B2 0x7000000000ull
// runtime/rt.h:32
#define SF2C_RT_N_0X7320000000ULL_D186 0x7320000000ull
// runtime/rt.h:34
#define SF2C_RT_N_0X7004000000ULL_4805 0x7004000000ull
// runtime/rt.h:35
#define SF2C_RT_N_0X7080000000ULL_BF09 0x7080000000ull
// runtime/rt.h:36
#define SF2C_RT_N_0X0000100000ULL_677C 0x0000100000ull
// runtime/rt.h:37
#define SF2C_RT_N_0X7090000000ULL_76A8 0x7090000000ull
// runtime/rt.h:39
#define SF2C_RT_N_0X7092000000ULL_BDF6 0x7092000000ull
// runtime/rt.h:41
#define SF2C_RT_N_0X7100000000ULL_A15E 0x7100000000ull
// runtime/rt.h:42
#define SF2C_RT_N_0X0100000000ULL_A561 0x0100000000ull
// runtime/rt.h:43
#define SF2C_RT_N_0X7240000000ULL_194E 0x7240000000ull
// runtime/rt.h:44
#define SF2C_RT_N_0X00D0000000ULL_6FF9 0x00D0000000ull
// runtime/rumble.h:8
#define SF2C_RUMBLE_N_16_B17E 16
// runtime/rumble.h:8
#define SF2C_RUMBLE_S_HID_VIBRATION_WIRE_LAYOUT_9D06 "HID vibration wire layout"
// runtime/rumble.h:12
#define SF2C_RUMBLE_N_65535_F2F8 65535
// runtime/rumble.h:13
#define SF2C_RUMBLE_N_65535_0F_A14C 65535.0f
// runtime/rumble.h:13
#define SF2C_RUMBLE_N_0_5F_2DB0 0.5f
// runtime/rumble.h:22
#define SF2C_RUMBLE_N_0XFFFF00FEU_3027 0xffff00feu
// runtime/rumble.h:22
#define SF2C_RUMBLE_N_0X03000000U_313C 0x03000000u
// runtime/rumble.h:23
#define SF2C_RUMBLE_N_255_9556 255
// runtime/rumble.h:25
#define SF2C_RUMBLE_N_0XFF000000U_E7BA 0xff000000u
// runtime/settings.c:14
#define SF2C_SETTINGS_S_JA_4A86 "ja"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_EN_US_C528 "en-US"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_EN_GB_90A2 "en-GB"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_FR_92E5 "fr"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_DE_FA20 "de"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_IT_FF56 "it"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_ES_14BA "es"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_ZH_CN_CB6A "zh-CN"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_ZH_TW_6F1B "zh-TW"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_RU_A8CA "ru"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_PT_04FA "pt"
// runtime/settings.c:14
#define SF2C_SETTINGS_S_KO_F0B0 "ko"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_JAPANESE_A013 "Japanese"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_ENGLISH_US_B197 "English (US)"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_ENGLISH_UK_A2A5 "English (UK)"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_FRENCH_0920 "French"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_GERMAN_AE4D "German"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_ITALIAN_E577 "Italian"
// runtime/settings.c:17
#define SF2C_SETTINGS_S_SPANISH_F67D "Spanish"
// runtime/settings.c:18
#define SF2C_SETTINGS_S_CHINESE_SIMPLIFIED_0D26 "Chinese (simplified)"
// runtime/settings.c:18
#define SF2C_SETTINGS_S_CHINESE_TRADITIONAL_A761 "Chinese (traditional)"
// runtime/settings.c:18
#define SF2C_SETTINGS_S_RUSSIAN_07BB "Russian"
// runtime/settings.c:18
#define SF2C_SETTINGS_S_PORTUGUESE_8CB4 "Portuguese"
// runtime/settings.c:18
#define SF2C_SETTINGS_S_KOREAN_ACDF "Korean"
// runtime/settings.c:24
#define SF2C_SETTINGS_N_1_0F_CA71 1.0f
// runtime/settings.c:28
#define SF2C_SETTINGS_N_60_39FA 60
// runtime/settings.c:36
#define SF2C_SETTINGS_N_100_AD57 100
// runtime/settings.c:56
#define SF2C_SETTINGS_S_SAVE_38AA "save"
// runtime/settings.c:67
#define SF2C_SETTINGS_S_SETTINGS_INVALID_S_S_EXPECTE_81F1 "[settings] invalid %s='%s', expected 0..1, keeping previous value\n"
// runtime/settings.c:73
#define SF2C_SETTINGS_S_TRUE_18D1 "true"
// runtime/settings.c:73
#define SF2C_SETTINGS_S_YES_6C76 "yes"
// runtime/settings.c:74
#define SF2C_SETTINGS_S_ON_990C "on"
// runtime/settings.c:74
#define SF2C_SETTINGS_S_1_3915 "1"
// runtime/settings.c:75
#define SF2C_SETTINGS_S_FALSE_076D "false"
// runtime/settings.c:75
#define SF2C_SETTINGS_S_NO_04A0 "no"
// runtime/settings.c:76
#define SF2C_SETTINGS_S_OFF_9F3B "off"
// runtime/settings.c:76
#define SF2C_SETTINGS_S_0_9808 "0"
// runtime/settings.c:97
#define SF2C_SETTINGS_S_LARECOMP_VOLUME_6C32 "LARECOMP_VOLUME"
// runtime/settings.c:98
#define SF2C_SETTINGS_S_LARECOMP_REVERB_419C "LARECOMP_REVERB"
// runtime/settings.c:100
#define SF2C_SETTINGS_S_SETTINGS_INVALID_LARECOMP_RE_ED26 "[settings] invalid LARECOMP_REVERB='%s', expected true/false, keeping previous value\n"
// runtime/settings.c:101
#define SF2C_SETTINGS_S_LARECOMP_SAVE_DIR_4AEE "LARECOMP_SAVE_DIR"
// runtime/settings.c:103
#define SF2C_SETTINGS_S_SETTINGS_INVALID_LARECOMP_SA_EC52 "[settings] invalid LARECOMP_SAVE_DIR, keeping previous save directory\n"
// runtime/settings.c:104
#define SF2C_SETTINGS_S_LARECOMP_LANG_74DD "LARECOMP_LANG"
// runtime/settings.c:106
#define SF2C_SETTINGS_S_AUTO_6346 "auto"
// runtime/settings.c:108
#define SF2C_SETTINGS_S_SETTINGS_UNKNOWN_LANGUAGE_CO_8725 "[settings] unknown language code %s (use one of %s...)\n"
// runtime/settings.c:116
#define SF2C_SETTINGS_S_LC_ALL_C2EB "LC_ALL"
// runtime/settings.c:116
#define SF2C_SETTINGS_S_LC_MESSAGES_49FD "LC_MESSAGES"
// runtime/settings.c:116
#define SF2C_SETTINGS_S_LANG_C3B4 "LANG"
// runtime/settings.c:132
#define SF2C_SETTINGS_N_64_A68B 64
// runtime/settings.c:133
#define SF2C_SETTINGS_S_EMPTY_9107 ".@"
// runtime/settings.c:137
#define SF2C_SETTINGS_CH_EMPTY_BDC4 '_'
// runtime/settings.c:137
#define SF2C_SETTINGS_CH_EMPTY_01F8 '-'
// runtime/settings.c:142
#define SF2C_SETTINGS_S_EMPTY_0933 "-"
// runtime/settings.c:159
#define SF2C_SETTINGS_S_GENERAL_A6F2 "general"
// runtime/settings.c:159
#define SF2C_SETTINGS_S_LANGUAGE_1182 "language"
// runtime/settings.c:160
#define SF2C_SETTINGS_S_NETWORK_57B8 "network"
// runtime/settings.c:160
#define SF2C_SETTINGS_S_ENABLED_6E30 "enabled"
// runtime/settings.c:161
#define SF2C_SETTINGS_S_GAMEPLAY_406B "gameplay"
// runtime/settings.c:161
#define SF2C_SETTINGS_S_UNLOCK_ALL_SPARRING_CHARACTE_8856 "unlock_all_sparring_characters"
// runtime/settings.c:162
#define SF2C_SETTINGS_S_ENABLE_SHADOW_IN_SPARRING_CAFB "enable_shadow_in_sparring"
// runtime/settings.c:163
#define SF2C_SETTINGS_S_ENABLE_TITAN_IN_SPARRING_3232 "enable_titan_in_sparring"
// runtime/settings.c:164
#define SF2C_SETTINGS_S_ALLOW_SPARRING_LAN_MULTIPLAY_290D "allow_sparring_lan_multiplayer"
// runtime/settings.c:165
#define SF2C_SETTINGS_S_UNLOCK_ALL_VIDEOS_A8A0 "unlock_all_videos"
// runtime/settings.c:166
#define SF2C_SETTINGS_S_SHOW_INTRO_88D4 "show_intro"
// runtime/settings.c:167
#define SF2C_SETTINGS_S_AI_ENABLED_8057 "ai_enabled"
// runtime/settings.c:168
#define SF2C_SETTINGS_S_SHOW_TIME_RESULTS_FC20 "show_time_results"
// runtime/settings.c:169
#define SF2C_SETTINGS_S_DEVELOPER_7E6C "developer"
// runtime/settings.c:169
#define SF2C_SETTINGS_S_CONSOLE_3593 "console"
// runtime/settings.c:170
#define SF2C_SETTINGS_S_TRAINER_EBE0 "trainer"
// runtime/settings.c:171
#define SF2C_SETTINGS_S_SHOP_CHEATS_9653 "shop_cheats"
// runtime/settings.c:172
#define SF2C_SETTINGS_S_OVERLAYS_178F "overlays"
// runtime/settings.c:173
#define SF2C_SETTINGS_S_OVERLAYS_ON_START_E5DA "overlays_on_start"
// runtime/settings.c:174
#define SF2C_SETTINGS_S_FPS_A9EF "fps"
// runtime/settings.c:175
#define SF2C_SETTINGS_S_MEMORY_E386 "memory"
// runtime/settings.c:176
#define SF2C_SETTINGS_S_BUILD_INFO_25FA "build_info"
// runtime/settings.c:177
#define SF2C_SETTINGS_S_MODEL_INFO_0D1C "model_info"
// runtime/settings.c:178
#define SF2C_SETTINGS_S_AUDIO_53B7 "audio"
// runtime/settings.c:178
#define SF2C_SETTINGS_S_VOLUME_3FF4 "volume"
// runtime/settings.c:179
#define SF2C_SETTINGS_S_REVERB_6A96 "reverb"
// runtime/settings.c:180
#define SF2C_SETTINGS_S_VIDEO_807F "video"
// runtime/settings.c:180
#define SF2C_SETTINGS_S_FULLSCREEN_962F "fullscreen"
// runtime/settings.c:181
#define SF2C_SETTINGS_S_ASPECT_RATIO_46A4 "aspect_ratio"
// runtime/settings.c:182
#define SF2C_SETTINGS_S_TARGET_FPS_10CA "target_fps"
// runtime/settings.c:183
#define SF2C_SETTINGS_S_INTERNAL_RESOLUTION_1625 "internal_resolution"
// runtime/settings.c:184
#define SF2C_SETTINGS_S_UPSCALER_3302 "upscaler"
// runtime/settings.c:185
#define SF2C_SETTINGS_S_ANTIALIASING_FC81 "antialiasing"
// runtime/settings.c:186
#define SF2C_SETTINGS_S_INPUT_48AE "input"
// runtime/settings.c:186
#define SF2C_SETTINGS_S_PROMPTS_6DA4 "prompts"
// runtime/settings.c:187
#define SF2C_SETTINGS_S_PLAYER_SETUP_C139 "player_setup"
// runtime/settings.c:188
#define SF2C_SETTINGS_S_STORAGE_6545 "storage"
// runtime/settings.c:188
#define SF2C_SETTINGS_S_USE_COMPLETED_SAVE_9A94 "use_completed_save"
// runtime/settings.c:189
#define SF2C_SETTINGS_S_SAVE_DIRECTORY_4C87 "save_directory"
// runtime/settings.c:203
#define SF2C_SETTINGS_CH_EMPTY_EB70 '\''
// runtime/settings.c:203
#define SF2C_SETTINGS_S_EMPTY_3251 '"'
// runtime/settings.c:205
#define SF2C_SETTINGS_CH_EMPTY_4734 '#'
// runtime/settings.c:205
#define SF2C_SETTINGS_CH_EMPTY_CF57 ';'
// runtime/settings.c:211
#define SF2C_SETTINGS_S_SOLO_C940 "solo"
// runtime/settings.c:212
#define SF2C_SETTINGS_S_KEYBOARD_VS_CONTROLLER_18CB "keyboard_vs_controller"
// runtime/settings.c:213
#define SF2C_SETTINGS_S_TWO_CONTROLLERS_4B78 "two_controllers"
// runtime/settings.c:216
#define SF2C_SETTINGS_S_ORIGINAL_4D60 "original"
// runtime/settings.c:216
#define SF2C_SETTINGS_S_16_9_0C60 "16:9"
// runtime/settings.c:217
#define SF2C_SETTINGS_S_16_10_7BA3 "16:10"
// runtime/settings.c:218
#define SF2C_SETTINGS_S_21_9_80DF "21:9"
// runtime/settings.c:219
#define SF2C_SETTINGS_S_32_9_307E "32:9"
// runtime/settings.c:220
#define SF2C_SETTINGS_S_NATIVE_4333 "native"
// runtime/settings.c:234
#define SF2C_SETTINGS_N_10_4A44 10
// runtime/settings.c:235
#define SF2C_SETTINGS_N_30_624B 30
// runtime/settings.c:235
#define SF2C_SETTINGS_N_360_838F 360
// runtime/settings.c:238
#define SF2C_SETTINGS_S_MATCH_7C82 "match"
// runtime/settings.c:243
#define SF2C_SETTINGS_S_DYNAMIC_68ED "dynamic"
// runtime/settings.c:244
#define SF2C_SETTINGS_S_0_25X_63EF "0.25x"
// runtime/settings.c:245
#define SF2C_SETTINGS_S_0_50X_1CF2 "0.50x"
// runtime/settings.c:245
#define SF2C_SETTINGS_S_0_5X_82D2 "0.5x"
// runtime/settings.c:246
#define SF2C_SETTINGS_S_0_75X_50AC "0.75x"
// runtime/settings.c:247
#define SF2C_SETTINGS_S_1X_9FF3 "1x"
// runtime/settings.c:248
#define SF2C_SETTINGS_S_2X_5383 "2x"
// runtime/settings.c:249
#define SF2C_SETTINGS_S_3X_2593 "3x"
// runtime/settings.c:250
#define SF2C_SETTINGS_S_4X_6992 "4x"
// runtime/settings.c:258
#define SF2C_SETTINGS_S_BILINEAR_70ED "bilinear"
// runtime/settings.c:259
#define SF2C_SETTINGS_S_FSR1_B074 "fsr1"
// runtime/settings.c:264
#define SF2C_SETTINGS_S_SMAA_4056 "smaa"
// runtime/settings.c:268
#define SF2C_SETTINGS_S_NINTENDO_AF74 "nintendo"
// runtime/settings.c:269
#define SF2C_SETTINGS_S_XBOX_76AE "xbox"
// runtime/settings.c:270
#define SF2C_SETTINGS_S_PLAYSTATION_15B2 "playstation"
// runtime/settings.c:283
#define SF2C_SETTINGS_S_R_2659 "r"
// runtime/settings.c:285
#define SF2C_SETTINGS_N_1024_E39E 1024
// runtime/settings.c:289
#define SF2C_SETTINGS_CH_N_AD65 '\n'
// runtime/settings.c:292
#define SF2C_SETTINGS_S_SETTINGS_S_U_LINE_TOO_LONG_I_ED4C "[settings] %s:%u line too long, ignored\n"
// runtime/settings.c:298
#define SF2C_SETTINGS_CH_EMPTY_854D '['
// runtime/settings.c:299
#define SF2C_SETTINGS_CH_EMPTY_CB80 ']'
// runtime/settings.c:304
#define SF2C_SETTINGS_S_S_0030 "%s"
// runtime/settings.c:307
#define SF2C_SETTINGS_CH_EMPTY_C874 '='
// runtime/settings.c:316
#define SF2C_SETTINGS_S_CONTROLS_KEYBOARD_4375 "controls.keyboard"
// runtime/settings.c:316
#define SF2C_SETTINGS_S_CONTROLS_GAMEPAD_C5B4 "controls.gamepad"
// runtime/settings.c:319
#define SF2C_SETTINGS_S_SETTINGS_S_U_INVALID_S_S_S_E_456B "[settings] %s:%u invalid [%s] %s='%s'; expected %s or none, keeping previous binding\n"
// runtime/settings.c:321
#define SF2C_SETTINGS_S_AN_SDL_SCANCODE_NAME_D232 "an SDL scancode name"
// runtime/settings.c:321
#define SF2C_SETTINGS_S_A_CONTROLLER_POSITION_EAST_S_EBEB "a controller position (east, south, ltrigger, ...)"
// runtime/settings.c:328
#define SF2C_SETTINGS_S_SETTINGS_S_U_INVALID_S_S_KEE_B6F4 "[settings] %s:%u invalid [%s] %s, keeping previous value\n"
// runtime/settings.c:336
#define SF2C_SETTINGS_S_SETTINGS_S_U_MALFORMED_LINE__C6E9 "[settings] %s:%u malformed line, ignored\n"
// runtime/settings.c:344
#define SF2C_SETTINGS_S_SF2_CONFIG_3707 "SF2_CONFIG"
// runtime/settings.c:346
#define SF2C_SETTINGS_S_SF2RECOMP_INI_9F07 "sf2recomp.ini"
// runtime/settings.c:347
#define SF2C_SETTINGS_S_SETTINGS_LOADED_S_N_D8C5 "[settings] loaded %s\n"
// runtime/settings.c:348
#define SF2C_SETTINGS_S_SETTINGS_CANNOT_READ_S_N_7365 "[settings] cannot read %s\n"
// runtime/settings.h:7
#define SF2C_SETTINGS_S_C_C2E8 "C"
// runtime/settings.h:11
#define SF2C_SETTINGS_N_11_4FC8 11
// runtime/settings.h:11
#define SF2C_SETTINGS_N_12_6B51 12
// runtime/settings.h:79
#define SF2C_SETTINGS_N_1U_B8E6 1u
// runtime/settings.h:80
#define SF2C_SETTINGS_N_16_B17E 16
// runtime/settings.h:82
#define SF2C_SETTINGS_N_21_6F4B 21
// runtime/settings.h:83
#define SF2C_SETTINGS_N_32_E29C 32
// runtime/simd.h:21
#define SF2C_SIMD_N_16_B17E 16
// runtime/simd.h:49
#define SF2C_SIMD_N_1U_B8E6 1u
// runtime/simd.h:49
#define SF2C_SIMD_N_26_5F9C 26
// runtime/simd.h:50
#define SF2C_SIMD_N_25_B7A5 25
// runtime/simd.h:51
#define SF2C_SIMD_N_24_C235 24
// runtime/simd.h:52
#define SF2C_SIMD_N_3U_3B80 3u
// runtime/simd.h:52
#define SF2C_SIMD_N_22_785F 22
// runtime/simd.h:58
#define SF2C_SIMD_N_256_51E8 256
// runtime/simd.h:59
#define SF2C_SIMD_N_255_9556 255
// runtime/simd.h:59
#define SF2C_SIMD_N_253_E786 253
// runtime/simd.h:59
#define SF2C_SIMD_N_251_C75D 251
// runtime/simd.h:59
#define SF2C_SIMD_N_249_9F48 249
// runtime/simd.h:59
#define SF2C_SIMD_N_247_396F 247
// runtime/simd.h:59
#define SF2C_SIMD_N_245_011A 245
// runtime/simd.h:59
#define SF2C_SIMD_N_243_7244 243
// runtime/simd.h:59
#define SF2C_SIMD_N_241_749F 241
// runtime/simd.h:59
#define SF2C_SIMD_N_240_6AF1 240
// runtime/simd.h:59
#define SF2C_SIMD_N_238_8AE4 238
// runtime/simd.h:59
#define SF2C_SIMD_N_236_9A04 236
// runtime/simd.h:59
#define SF2C_SIMD_N_234_114B 234
// runtime/simd.h:59
#define SF2C_SIMD_N_232_835D 232
// runtime/simd.h:59
#define SF2C_SIMD_N_230_A0EA 230
// runtime/simd.h:59
#define SF2C_SIMD_N_229_0849 229
// runtime/simd.h:59
#define SF2C_SIMD_N_227_DFE6 227
// runtime/simd.h:60
#define SF2C_SIMD_N_225_0E65 225
// runtime/simd.h:60
#define SF2C_SIMD_N_223_56F4 223
// runtime/simd.h:60
#define SF2C_SIMD_N_221_67E9 221
// runtime/simd.h:60
#define SF2C_SIMD_N_220_3679 220
// runtime/simd.h:60
#define SF2C_SIMD_N_218_5966 218
// runtime/simd.h:60
#define SF2C_SIMD_N_216_0F41 216
// runtime/simd.h:60
#define SF2C_SIMD_N_215_D865 215
// runtime/simd.h:60
#define SF2C_SIMD_N_213_D48F 213
// runtime/simd.h:60
#define SF2C_SIMD_N_211_0934 211
// runtime/simd.h:60
#define SF2C_SIMD_N_210_D29D 210
// runtime/simd.h:60
#define SF2C_SIMD_N_208_8DF6 208
// runtime/simd.h:60
#define SF2C_SIMD_N_206_5CF4 206
// runtime/simd.h:60
#define SF2C_SIMD_N_205_F880 205
// runtime/simd.h:60
#define SF2C_SIMD_N_203_4621 203
// runtime/simd.h:60
#define SF2C_SIMD_N_201_4397 201
// runtime/simd.h:60
#define SF2C_SIMD_N_200_27BA 200
// runtime/simd.h:61
#define SF2C_SIMD_N_198_A4E0 198
// runtime/simd.h:61
#define SF2C_SIMD_N_197_8BCB 197
// runtime/simd.h:61
#define SF2C_SIMD_N_195_1DFA 195
// runtime/simd.h:61
#define SF2C_SIMD_N_194_7559 194
// runtime/simd.h:61
#define SF2C_SIMD_N_192_EB3B 192
// runtime/simd.h:61
#define SF2C_SIMD_N_191_7026 191
// runtime/simd.h:61
#define SF2C_SIMD_N_189_7045 189
// runtime/simd.h:61
#define SF2C_SIMD_N_188_D606 188
// runtime/simd.h:61
#define SF2C_SIMD_N_186_2811 186
// runtime/simd.h:61
#define SF2C_SIMD_N_185_61A2 185
// runtime/simd.h:61
#define SF2C_SIMD_N_183_B8AE 183
// runtime/simd.h:61
#define SF2C_SIMD_N_182_BFA7 182
// runtime/simd.h:61
#define SF2C_SIMD_N_180_7B69 180
// runtime/simd.h:61
#define SF2C_SIMD_N_179_3068 179
// runtime/simd.h:61
#define SF2C_SIMD_N_177_8CD2 177
// runtime/simd.h:61
#define SF2C_SIMD_N_176_CBA2 176
// runtime/simd.h:62
#define SF2C_SIMD_N_174_41E5 174
// runtime/simd.h:62
#define SF2C_SIMD_N_173_4A85 173
// runtime/simd.h:62
#define SF2C_SIMD_N_172_6851 172
// runtime/simd.h:62
#define SF2C_SIMD_N_170_734D 170
// runtime/simd.h:62
#define SF2C_SIMD_N_169_F57E 169
// runtime/simd.h:62
#define SF2C_SIMD_N_167_73D3 167
// runtime/simd.h:62
#define SF2C_SIMD_N_166_E0F0 166
// runtime/simd.h:62
#define SF2C_SIMD_N_165_BC52 165
// runtime/simd.h:62
#define SF2C_SIMD_N_163_3D32 163
// runtime/simd.h:62
#define SF2C_SIMD_N_162_79D6 162
// runtime/simd.h:62
#define SF2C_SIMD_N_161_BB66 161
// runtime/simd.h:62
#define SF2C_SIMD_N_159_FF2C 159
// runtime/simd.h:62
#define SF2C_SIMD_N_158_7ED8 158
// runtime/simd.h:62
#define SF2C_SIMD_N_157_C75D 157
// runtime/simd.h:62
#define SF2C_SIMD_N_156_0FEC 156
// runtime/simd.h:62
#define SF2C_SIMD_N_154_1D0E 154
// runtime/simd.h:63
#define SF2C_SIMD_N_153_620C 153
// runtime/simd.h:63
#define SF2C_SIMD_N_152_0430 152
// runtime/simd.h:63
#define SF2C_SIMD_N_150_9AE2 150
// runtime/simd.h:63
#define SF2C_SIMD_N_149_05AD 149
// runtime/simd.h:63
#define SF2C_SIMD_N_148_EC2E 148
// runtime/simd.h:63
#define SF2C_SIMD_N_147_1D28 147
// runtime/simd.h:63
#define SF2C_SIMD_N_145_BE47 145
// runtime/simd.h:63
#define SF2C_SIMD_N_144_5EC1 144
// runtime/simd.h:63
#define SF2C_SIMD_N_143_D6F0 143
// runtime/simd.h:63
#define SF2C_SIMD_N_142_D4EE 142
// runtime/simd.h:63
#define SF2C_SIMD_N_141_2C7D 141
// runtime/simd.h:63
#define SF2C_SIMD_N_139_8D27 139
// runtime/simd.h:63
#define SF2C_SIMD_N_138_D6A4 138
// runtime/simd.h:63
#define SF2C_SIMD_N_137_D80E 137
// runtime/simd.h:63
#define SF2C_SIMD_N_136_36EB 136
// runtime/simd.h:63
#define SF2C_SIMD_N_135_1367 135
// runtime/simd.h:64
#define SF2C_SIMD_N_134_5D38 134
// runtime/simd.h:64
#define SF2C_SIMD_N_132_DBB1 132
// runtime/simd.h:64
#define SF2C_SIMD_N_131_EECA 131
// runtime/simd.h:64
#define SF2C_SIMD_N_130_38D6 130
// runtime/simd.h:64
#define SF2C_SIMD_N_129_6566 129
// runtime/simd.h:64
#define SF2C_SIMD_N_128_2747 128
// runtime/simd.h:64
#define SF2C_SIMD_N_127_922C 127
// runtime/simd.h:64
#define SF2C_SIMD_N_126_65A6 126
// runtime/simd.h:64
#define SF2C_SIMD_N_124_6AFF 124
// runtime/simd.h:64
#define SF2C_SIMD_N_123_A665 123
// runtime/simd.h:64
#define SF2C_SIMD_N_122_1BE0 122
// runtime/simd.h:64
#define SF2C_SIMD_N_121_89AA 121
// runtime/simd.h:64
#define SF2C_SIMD_N_120_2ABA 120
// runtime/simd.h:64
#define SF2C_SIMD_N_119_3038 119
// runtime/simd.h:64
#define SF2C_SIMD_N_118_85DA 118
// runtime/simd.h:64
#define SF2C_SIMD_N_117_2AC8 117
// runtime/simd.h:65
#define SF2C_SIMD_N_116_E5B8 116
// runtime/simd.h:65
#define SF2C_SIMD_N_115_28DA 115
// runtime/simd.h:65
#define SF2C_SIMD_N_114_9F1F 114
// runtime/simd.h:65
#define SF2C_SIMD_N_113_6C65 113
// runtime/simd.h:65
#define SF2C_SIMD_N_112_B155 112
// runtime/simd.h:65
#define SF2C_SIMD_N_111_F6E0 111
// runtime/simd.h:65
#define SF2C_SIMD_N_110_9BDB 110
// runtime/simd.h:65
#define SF2C_SIMD_N_109_0FD4 109
// runtime/simd.h:65
#define SF2C_SIMD_N_108_9537 108
// runtime/simd.h:65
#define SF2C_SIMD_N_107_3346 107
// runtime/simd.h:65
#define SF2C_SIMD_N_106_482D 106
// runtime/simd.h:65
#define SF2C_SIMD_N_105_1253 105
// runtime/simd.h:65
#define SF2C_SIMD_N_104_5EF6 104
// runtime/simd.h:65
#define SF2C_SIMD_N_103_454F 103
// runtime/simd.h:65
#define SF2C_SIMD_N_102_3783 102
// runtime/simd.h:65
#define SF2C_SIMD_N_101_16DC 101
// runtime/simd.h:66
#define SF2C_SIMD_N_100_AD57 100
// runtime/simd.h:66
#define SF2C_SIMD_N_99_8C1F 99
// runtime/simd.h:66
#define SF2C_SIMD_N_98_29DB 98
// runtime/simd.h:66
#define SF2C_SIMD_N_97_D6D8 97
// runtime/simd.h:66
#define SF2C_SIMD_N_96_7B1A 96
// runtime/simd.h:66
#define SF2C_SIMD_N_95_AD48 95
// runtime/simd.h:66
#define SF2C_SIMD_N_94_E3D6 94
// runtime/simd.h:66
#define SF2C_SIMD_N_93_6E40 93
// runtime/simd.h:66
#define SF2C_SIMD_N_92_8241 92
// runtime/simd.h:66
#define SF2C_SIMD_N_91_1DA5 91
// runtime/simd.h:66
#define SF2C_SIMD_N_90_69F5 90
// runtime/simd.h:66
#define SF2C_SIMD_N_89_CD70 89
// runtime/simd.h:66
#define SF2C_SIMD_N_88_8B94 88
// runtime/simd.h:66
#define SF2C_SIMD_N_87_BDD2 87
// runtime/simd.h:66
#define SF2C_SIMD_N_86_434C 86
// runtime/simd.h:67
#define SF2C_SIMD_N_85_B494 85
// runtime/simd.h:67
#define SF2C_SIMD_N_84_44C8 84
// runtime/simd.h:67
#define SF2C_SIMD_N_83_BBB9 83
// runtime/simd.h:67
#define SF2C_SIMD_N_82_A46E 82
// runtime/simd.h:67
#define SF2C_SIMD_N_81_5316 81
// runtime/simd.h:67
#define SF2C_SIMD_N_80_4844 80
// runtime/simd.h:67
#define SF2C_SIMD_N_79_98A3 79
// runtime/simd.h:67
#define SF2C_SIMD_N_78_349C 78
// runtime/simd.h:67
#define SF2C_SIMD_N_77_A88A 77
// runtime/simd.h:67
#define SF2C_SIMD_N_76_F74E 76
// runtime/simd.h:67
#define SF2C_SIMD_N_75_F369 75
// runtime/simd.h:67
#define SF2C_SIMD_N_74_EB62 74
// runtime/simd.h:67
#define SF2C_SIMD_N_73_9606 73
// runtime/simd.h:67
#define SF2C_SIMD_N_72_8722 72
// runtime/simd.h:68
#define SF2C_SIMD_N_71_7F22 71
// runtime/simd.h:68
#define SF2C_SIMD_N_70_FF5A 70
// runtime/simd.h:68
#define SF2C_SIMD_N_69_C75C 69
// runtime/simd.h:68
#define SF2C_SIMD_N_68_A218 68
// runtime/simd.h:68
#define SF2C_SIMD_N_67_49D1 67
// runtime/simd.h:68
#define SF2C_SIMD_N_66_3ADA 66
// runtime/simd.h:68
#define SF2C_SIMD_N_65_108C 65
// runtime/simd.h:68
#define SF2C_SIMD_N_64_A68B 64
// runtime/simd.h:68
#define SF2C_SIMD_N_63_DA4E 63
// runtime/simd.h:68
#define SF2C_SIMD_N_62_81B8 62
// runtime/simd.h:68
#define SF2C_SIMD_N_61_D029 61
// runtime/simd.h:68
#define SF2C_SIMD_N_60_39FA 60
// runtime/simd.h:68
#define SF2C_SIMD_N_59_3E1E 59
// runtime/simd.h:69
#define SF2C_SIMD_N_58_6208 58
// runtime/simd.h:69
#define SF2C_SIMD_N_57_C837 57
// runtime/simd.h:69
#define SF2C_SIMD_N_56_7688 56
// runtime/simd.h:69
#define SF2C_SIMD_N_55_02D2 55
// runtime/simd.h:69
#define SF2C_SIMD_N_54_2FCA 54
// runtime/simd.h:69
#define SF2C_SIMD_N_53_2858 53
// runtime/simd.h:69
#define SF2C_SIMD_N_52_41CF 52
// runtime/simd.h:69
#define SF2C_SIMD_N_51_031B 51
// runtime/simd.h:69
#define SF2C_SIMD_N_50_1A65 50
// runtime/simd.h:69
#define SF2C_SIMD_N_49_0E17 49
// runtime/simd.h:69
#define SF2C_SIMD_N_48_9801 48
// runtime/simd.h:70
#define SF2C_SIMD_N_47_3148 47
// runtime/simd.h:70
#define SF2C_SIMD_N_46_25FC 46
// runtime/simd.h:70
#define SF2C_SIMD_N_45_8117 45
// runtime/simd.h:70
#define SF2C_SIMD_N_44_71EE 44
// runtime/simd.h:70
#define SF2C_SIMD_N_43_44CB 43
// runtime/simd.h:70
#define SF2C_SIMD_N_42_7347 42
// runtime/simd.h:70
#define SF2C_SIMD_N_41_3D91 41
// runtime/simd.h:70
#define SF2C_SIMD_N_40_D59E 40
// runtime/simd.h:70
#define SF2C_SIMD_N_39_0B91 39
// runtime/simd.h:70
#define SF2C_SIMD_N_38_AEA9 38
// runtime/simd.h:70
#define SF2C_SIMD_N_37_7A61 37
// runtime/simd.h:71
#define SF2C_SIMD_N_36_76A5 36
// runtime/simd.h:71
#define SF2C_SIMD_N_35_9F14 35
// runtime/simd.h:71
#define SF2C_SIMD_N_34_86E5 34
// runtime/simd.h:71
#define SF2C_SIMD_N_33_C6F3 33
// runtime/simd.h:71
#define SF2C_SIMD_N_32_E29C 32
// runtime/simd.h:71
#define SF2C_SIMD_N_31_EB1E 31
// runtime/simd.h:71
#define SF2C_SIMD_N_30_624B 30
// runtime/simd.h:71
#define SF2C_SIMD_N_29_3513 29
// runtime/simd.h:71
#define SF2C_SIMD_N_28_59E1 28
// runtime/simd.h:71
#define SF2C_SIMD_N_27_6706 27
// runtime/simd.h:72
#define SF2C_SIMD_N_23_535F 23
// runtime/simd.h:72
#define SF2C_SIMD_N_21_6F4B 21
// runtime/simd.h:72
#define SF2C_SIMD_N_20_F5CA 20
// runtime/simd.h:72
#define SF2C_SIMD_N_19_9400 19
// runtime/simd.h:72
#define SF2C_SIMD_N_18_4EC9 18
// runtime/simd.h:72
#define SF2C_SIMD_N_17_4523 17
// runtime/simd.h:73
#define SF2C_SIMD_N_15_E629 15
// runtime/simd.h:73
#define SF2C_SIMD_N_14_8527 14
// runtime/simd.h:73
#define SF2C_SIMD_N_13_3FDB 13
// runtime/simd.h:73
#define SF2C_SIMD_N_12_6B51 12
// runtime/simd.h:73
#define SF2C_SIMD_N_11_4FC8 11
// runtime/simd.h:73
#define SF2C_SIMD_N_10_4A44 10
// runtime/simd.h:76
#define SF2C_SIMD_N_384_37B7 384
// runtime/simd.h:77
#define SF2C_SIMD_N_242_1406 242
// runtime/simd.h:77
#define SF2C_SIMD_N_233_C050 233
// runtime/simd.h:77
#define SF2C_SIMD_N_231_138D 231
// runtime/simd.h:77
#define SF2C_SIMD_N_228_9D69 228
// runtime/simd.h:78
#define SF2C_SIMD_N_226_8F1F 226
// runtime/simd.h:78
#define SF2C_SIMD_N_224_84A5 224
// runtime/simd.h:78
#define SF2C_SIMD_N_219_314F 219
// runtime/simd.h:78
#define SF2C_SIMD_N_212_FA2B 212
// runtime/simd.h:78
#define SF2C_SIMD_N_209_83F8 209
// runtime/simd.h:78
#define SF2C_SIMD_N_207_9680 207
// runtime/simd.h:78
#define SF2C_SIMD_N_204_FC56 204
// runtime/simd.h:79
#define SF2C_SIMD_N_196_B4BB 196
// runtime/simd.h:79
#define SF2C_SIMD_N_193_684F 193
// runtime/simd.h:79
#define SF2C_SIMD_N_190_2397 190
// runtime/simd.h:79
#define SF2C_SIMD_N_184_52F1 184
// runtime/simd.h:79
#define SF2C_SIMD_N_181_5808 181
// runtime/simd.h:80
#define SF2C_SIMD_N_178_01D5 178
// runtime/simd.h:80
#define SF2C_SIMD_N_175_DAC5 175
// runtime/simd.h:80
#define SF2C_SIMD_N_168_80C3 168
// runtime/simd.h:80
#define SF2C_SIMD_N_164_3F98 164
// runtime/simd.h:81
#define SF2C_SIMD_N_160_A512 160
// runtime/simd.h:81
#define SF2C_SIMD_N_155_210E 155
// runtime/simd.h:81
#define SF2C_SIMD_N_151_8E61 151
// runtime/simd.h:81
#define SF2C_SIMD_N_146_0A5B 146
// runtime/simd.h:82
#define SF2C_SIMD_N_140_DBAE 140
// runtime/simd.h:82
#define SF2C_SIMD_N_133_D2F4 133
// runtime/simd.h:83
#define SF2C_SIMD_N_125_0F8E 125
// runtime/simd.h:103
#define SF2C_SIMD_N_0X00000000_A40A 0x00000000
// runtime/simd.h:103
#define SF2C_SIMD_N_0X77073096_B90F 0x77073096
// runtime/simd.h:103
#define SF2C_SIMD_N_0XEE0E612C_3938 0xee0e612c
// runtime/simd.h:103
#define SF2C_SIMD_N_0X990951BA_16F0 0x990951ba
// runtime/simd.h:103
#define SF2C_SIMD_N_0X076DC419_AA41 0x076dc419
// runtime/simd.h:103
#define SF2C_SIMD_N_0X706AF48F_8160 0x706af48f
// runtime/simd.h:103
#define SF2C_SIMD_N_0XE963A535_36B8 0xe963a535
// runtime/simd.h:103
#define SF2C_SIMD_N_0X9E6495A3_4F2C 0x9e6495a3
// runtime/simd.h:104
#define SF2C_SIMD_N_0X0EDB8832_3160 0x0edb8832
// runtime/simd.h:104
#define SF2C_SIMD_N_0X79DCB8A4_1E3A 0x79dcb8a4
// runtime/simd.h:104
#define SF2C_SIMD_N_0XE0D5E91E_59CE 0xe0d5e91e
// runtime/simd.h:104
#define SF2C_SIMD_N_0X97D2D988_4A79 0x97d2d988
// runtime/simd.h:104
#define SF2C_SIMD_N_0X09B64C2B_1C24 0x09b64c2b
// runtime/simd.h:104
#define SF2C_SIMD_N_0X7EB17CBD_385C 0x7eb17cbd
// runtime/simd.h:104
#define SF2C_SIMD_N_0XE7B82D07_878B 0xe7b82d07
// runtime/simd.h:104
#define SF2C_SIMD_N_0X90BF1D91_CD47 0x90bf1d91
// runtime/simd.h:105
#define SF2C_SIMD_N_0X1DB71064_D8FF 0x1db71064
// runtime/simd.h:105
#define SF2C_SIMD_N_0X6AB020F2_06CD 0x6ab020f2
// runtime/simd.h:105
#define SF2C_SIMD_N_0XF3B97148_D0C3 0xf3b97148
// runtime/simd.h:105
#define SF2C_SIMD_N_0X84BE41DE_5115 0x84be41de
// runtime/simd.h:105
#define SF2C_SIMD_N_0X1ADAD47D_FD46 0x1adad47d
// runtime/simd.h:105
#define SF2C_SIMD_N_0X6DDDE4EB_E36F 0x6ddde4eb
// runtime/simd.h:105
#define SF2C_SIMD_N_0XF4D4B551_B829 0xf4d4b551
// runtime/simd.h:105
#define SF2C_SIMD_N_0X83D385C7_D64C 0x83d385c7
// runtime/simd.h:106
#define SF2C_SIMD_N_0X136C9856_1BE0 0x136c9856
// runtime/simd.h:106
#define SF2C_SIMD_N_0X646BA8C0_F5AF 0x646ba8c0
// runtime/simd.h:106
#define SF2C_SIMD_N_0XFD62F97A_0A4B 0xfd62f97a
// runtime/simd.h:106
#define SF2C_SIMD_N_0X8A65C9EC_81BE 0x8a65c9ec
// runtime/simd.h:106
#define SF2C_SIMD_N_0X14015C4F_775A 0x14015c4f
// runtime/simd.h:106
#define SF2C_SIMD_N_0X63066CD9_438D 0x63066cd9
// runtime/simd.h:106
#define SF2C_SIMD_N_0XFA0F3D63_91CF 0xfa0f3d63
// runtime/simd.h:106
#define SF2C_SIMD_N_0X8D080DF5_28C1 0x8d080df5
// runtime/simd.h:107
#define SF2C_SIMD_N_0X3B6E20C8_3F1A 0x3b6e20c8
// runtime/simd.h:107
#define SF2C_SIMD_N_0X4C69105E_0438 0x4c69105e
// runtime/simd.h:107
#define SF2C_SIMD_N_0XD56041E4_74AF 0xd56041e4
// runtime/simd.h:107
#define SF2C_SIMD_N_0XA2677172_951D 0xa2677172
// runtime/simd.h:107
#define SF2C_SIMD_N_0X3C03E4D1_878B 0x3c03e4d1
// runtime/simd.h:107
#define SF2C_SIMD_N_0X4B04D447_287C 0x4b04d447
// runtime/simd.h:107
#define SF2C_SIMD_N_0XD20D85FD_5987 0xd20d85fd
// runtime/simd.h:107
#define SF2C_SIMD_N_0XA50AB56B_94A6 0xa50ab56b
// runtime/simd.h:108
#define SF2C_SIMD_N_0X35B5A8FA_EA85 0x35b5a8fa
// runtime/simd.h:108
#define SF2C_SIMD_N_0X42B2986C_A2C3 0x42b2986c
// runtime/simd.h:108
#define SF2C_SIMD_N_0XDBBBC9D6_2BB2 0xdbbbc9d6
// runtime/simd.h:108
#define SF2C_SIMD_N_0XACBCF940_85C2 0xacbcf940
// runtime/simd.h:108
#define SF2C_SIMD_N_0X32D86CE3_55C0 0x32d86ce3
// runtime/simd.h:108
#define SF2C_SIMD_N_0X45DF5C75_F9E8 0x45df5c75
// runtime/simd.h:108
#define SF2C_SIMD_N_0XDCD60DCF_49B1 0xdcd60dcf
// runtime/simd.h:108
#define SF2C_SIMD_N_0XABD13D59_8BCF 0xabd13d59
// runtime/simd.h:109
#define SF2C_SIMD_N_0X26D930AC_4C5C 0x26d930ac
// runtime/simd.h:109
#define SF2C_SIMD_N_0X51DE003A_B560 0x51de003a
// runtime/simd.h:109
#define SF2C_SIMD_N_0XC8D75180_1FCF 0xc8d75180
// runtime/simd.h:109
#define SF2C_SIMD_N_0XBFD06116_318D 0xbfd06116
// runtime/simd.h:109
#define SF2C_SIMD_N_0X21B4F4B5_24EF 0x21b4f4b5
// runtime/simd.h:109
#define SF2C_SIMD_N_0X56B3C423_2953 0x56b3c423
// runtime/simd.h:109
#define SF2C_SIMD_N_0XCFBA9599_7C90 0xcfba9599
// runtime/simd.h:109
#define SF2C_SIMD_N_0XB8BDA50F_D93D 0xb8bda50f
// runtime/simd.h:110
#define SF2C_SIMD_N_0X2802B89E_A3BA 0x2802b89e
// runtime/simd.h:110
#define SF2C_SIMD_N_0X5F058808_12A9 0x5f058808
// runtime/simd.h:110
#define SF2C_SIMD_N_0XC60CD9B2_B28B 0xc60cd9b2
// runtime/simd.h:110
#define SF2C_SIMD_N_0XB10BE924_0919 0xb10be924
// runtime/simd.h:110
#define SF2C_SIMD_N_0X2F6F7C87_C15A 0x2f6f7c87
// runtime/simd.h:110
#define SF2C_SIMD_N_0X58684C11_FEAE 0x58684c11
// runtime/simd.h:110
#define SF2C_SIMD_N_0XC1611DAB_1B59 0xc1611dab
// runtime/simd.h:110
#define SF2C_SIMD_N_0XB6662D3D_BF77 0xb6662d3d
// runtime/simd.h:111
#define SF2C_SIMD_N_0X76DC4190_ADB6 0x76dc4190
// runtime/simd.h:111
#define SF2C_SIMD_N_0X01DB7106_D909 0x01db7106
// runtime/simd.h:111
#define SF2C_SIMD_N_0X98D220BC_C5C6 0x98d220bc
// runtime/simd.h:111
#define SF2C_SIMD_N_0XEFD5102A_82B8 0xefd5102a
// runtime/simd.h:111
#define SF2C_SIMD_N_0X71B18589_3C4B 0x71b18589
// runtime/simd.h:111
#define SF2C_SIMD_N_0X06B6B51F_31D0 0x06b6b51f
// runtime/simd.h:111
#define SF2C_SIMD_N_0X9FBFE4A5_4180 0x9fbfe4a5
// runtime/simd.h:111
#define SF2C_SIMD_N_0XE8B8D433_8D28 0xe8b8d433
// runtime/simd.h:112
#define SF2C_SIMD_N_0X7807C9A2_1455 0x7807c9a2
// runtime/simd.h:112
#define SF2C_SIMD_N_0X0F00F934_13D6 0x0f00f934
// runtime/simd.h:112
#define SF2C_SIMD_N_0X9609A88E_9135 0x9609a88e
// runtime/simd.h:112
#define SF2C_SIMD_N_0XE10E9818_D282 0xe10e9818
// runtime/simd.h:112
#define SF2C_SIMD_N_0X7F6A0DBB_5EF1 0x7f6a0dbb
// runtime/simd.h:112
#define SF2C_SIMD_N_0X086D3D2D_A2BF 0x086d3d2d
// runtime/simd.h:112
#define SF2C_SIMD_N_0X91646C97_75BE 0x91646c97
// runtime/simd.h:112
#define SF2C_SIMD_N_0XE6635C01_E65C 0xe6635c01
// runtime/simd.h:113
#define SF2C_SIMD_N_0X6B6B51F4_CE66 0x6b6b51f4
// runtime/simd.h:113
#define SF2C_SIMD_N_0X1C6C6162_8F0A 0x1c6c6162
// runtime/simd.h:113
#define SF2C_SIMD_N_0X856530D8_7DBE 0x856530d8
// runtime/simd.h:113
#define SF2C_SIMD_N_0XF262004E_5D2B 0xf262004e
// runtime/simd.h:113
#define SF2C_SIMD_N_0X6C0695ED_55D4 0x6c0695ed
// runtime/simd.h:113
#define SF2C_SIMD_N_0X1B01A57B_5B11 0x1b01a57b
// runtime/simd.h:113
#define SF2C_SIMD_N_0X8208F4C1_CAC3 0x8208f4c1
// runtime/simd.h:113
#define SF2C_SIMD_N_0XF50FC457_C81D 0xf50fc457
// runtime/simd.h:114
#define SF2C_SIMD_N_0X65B0D9C6_2670 0x65b0d9c6
// runtime/simd.h:114
#define SF2C_SIMD_N_0X12B7E950_18FD 0x12b7e950
// runtime/simd.h:114
#define SF2C_SIMD_N_0X8BBEB8EA_5681 0x8bbeb8ea
// runtime/simd.h:114
#define SF2C_SIMD_N_0XFCB9887C_6CC3 0xfcb9887c
// runtime/simd.h:114
#define SF2C_SIMD_N_0X62DD1DDF_F485 0x62dd1ddf
// runtime/simd.h:114
#define SF2C_SIMD_N_0X15DA2D49_8271 0x15da2d49
// runtime/simd.h:114
#define SF2C_SIMD_N_0X8CD37CF3_6DDE 0x8cd37cf3
// runtime/simd.h:114
#define SF2C_SIMD_N_0XFBD44C65_12D1 0xfbd44c65
// runtime/simd.h:115
#define SF2C_SIMD_N_0X4DB26158_0D95 0x4db26158
// runtime/simd.h:115
#define SF2C_SIMD_N_0X3AB551CE_0632 0x3ab551ce
// runtime/simd.h:115
#define SF2C_SIMD_N_0XA3BC0074_79E2 0xa3bc0074
// runtime/simd.h:115
#define SF2C_SIMD_N_0XD4BB30E2_1198 0xd4bb30e2
// runtime/simd.h:115
#define SF2C_SIMD_N_0X4ADFA541_2188 0x4adfa541
// runtime/simd.h:115
#define SF2C_SIMD_N_0X3DD895D7_A960 0x3dd895d7
// runtime/simd.h:115
#define SF2C_SIMD_N_0XA4D1C46D_E7DA 0xa4d1c46d
// runtime/simd.h:115
#define SF2C_SIMD_N_0XD3D6F4FB_D33D 0xd3d6f4fb
// runtime/simd.h:116
#define SF2C_SIMD_N_0X4369E96A_F29C 0x4369e96a
// runtime/simd.h:116
#define SF2C_SIMD_N_0X346ED9FC_F420 0x346ed9fc
// runtime/simd.h:116
#define SF2C_SIMD_N_0XAD678846_3E76 0xad678846
// runtime/simd.h:116
#define SF2C_SIMD_N_0XDA60B8D0_02AB 0xda60b8d0
// runtime/simd.h:116
#define SF2C_SIMD_N_0X44042D73_050D 0x44042d73
// runtime/simd.h:116
#define SF2C_SIMD_N_0X33031DE5_E48A 0x33031de5
// runtime/simd.h:116
#define SF2C_SIMD_N_0XAA0A4C5F_8CA8 0xaa0a4c5f
// runtime/simd.h:116
#define SF2C_SIMD_N_0XDD0D7CC9_1ED8 0xdd0d7cc9
// runtime/simd.h:117
#define SF2C_SIMD_N_0X5005713C_3962 0x5005713c
// runtime/simd.h:117
#define SF2C_SIMD_N_0X270241AA_7E8C 0x270241aa
// runtime/simd.h:117
#define SF2C_SIMD_N_0XBE0B1010_B88D 0xbe0b1010
// runtime/simd.h:117
#define SF2C_SIMD_N_0XC90C2086_C31C 0xc90c2086
// runtime/simd.h:117
#define SF2C_SIMD_N_0X5768B525_9C5B 0x5768b525
// runtime/simd.h:117
#define SF2C_SIMD_N_0X206F85B3_023C 0x206f85b3
// runtime/simd.h:117
#define SF2C_SIMD_N_0XB966D409_8167 0xb966d409
// runtime/simd.h:117
#define SF2C_SIMD_N_0XCE61E49F_DB3A 0xce61e49f
// runtime/simd.h:118
#define SF2C_SIMD_N_0X5EDEF90E_A353 0x5edef90e
// runtime/simd.h:118
#define SF2C_SIMD_N_0X29D9C998_069B 0x29d9c998
// runtime/simd.h:118
#define SF2C_SIMD_N_0XB0D09822_8A4D 0xb0d09822
// runtime/simd.h:118
#define SF2C_SIMD_N_0XC7D7A8B4_7C13 0xc7d7a8b4
// runtime/simd.h:118
#define SF2C_SIMD_N_0X59B33D17_61F3 0x59b33d17
// runtime/simd.h:118
#define SF2C_SIMD_N_0X2EB40D81_4F48 0x2eb40d81
// runtime/simd.h:118
#define SF2C_SIMD_N_0XB7BD5C3B_66BE 0xb7bd5c3b
// runtime/simd.h:118
#define SF2C_SIMD_N_0XC0BA6CAD_CF51 0xc0ba6cad
// runtime/simd.h:119
#define SF2C_SIMD_N_0XEDB88320_D6F1 0xedb88320
// runtime/simd.h:119
#define SF2C_SIMD_N_0X9ABFB3B6_0C22 0x9abfb3b6
// runtime/simd.h:119
#define SF2C_SIMD_N_0X03B6E20C_6B3A 0x03b6e20c
// runtime/simd.h:119
#define SF2C_SIMD_N_0X74B1D29A_946B 0x74b1d29a
// runtime/simd.h:119
#define SF2C_SIMD_N_0XEAD54739_2917 0xead54739
// runtime/simd.h:119
#define SF2C_SIMD_N_0X9DD277AF_067C 0x9dd277af
// runtime/simd.h:119
#define SF2C_SIMD_N_0X04DB2615_6758 0x04db2615
// runtime/simd.h:119
#define SF2C_SIMD_N_0X73DC1683_CCAB 0x73dc1683
// runtime/simd.h:120
#define SF2C_SIMD_N_0XE3630B12_904B 0xe3630b12
// runtime/simd.h:120
#define SF2C_SIMD_N_0X94643B84_0947 0x94643b84
// runtime/simd.h:120
#define SF2C_SIMD_N_0X0D6D6A3E_56F4 0x0d6d6a3e
// runtime/simd.h:120
#define SF2C_SIMD_N_0X7A6A5AA8_0FDA 0x7a6a5aa8
// runtime/simd.h:120
#define SF2C_SIMD_N_0XE40ECF0B_989E 0xe40ecf0b
// runtime/simd.h:120
#define SF2C_SIMD_N_0X9309FF9D_CEBD 0x9309ff9d
// runtime/simd.h:120
#define SF2C_SIMD_N_0X0A00AE27_340C 0x0a00ae27
// runtime/simd.h:120
#define SF2C_SIMD_N_0X7D079EB1_5D58 0x7d079eb1
// runtime/simd.h:121
#define SF2C_SIMD_N_0XF00F9344_CFB1 0xf00f9344
// runtime/simd.h:121
#define SF2C_SIMD_N_0X8708A3D2_FCB8 0x8708a3d2
// runtime/simd.h:121
#define SF2C_SIMD_N_0X1E01F268_7CF2 0x1e01f268
// runtime/simd.h:121
#define SF2C_SIMD_N_0X6906C2FE_DF63 0x6906c2fe
// runtime/simd.h:121
#define SF2C_SIMD_N_0XF762575D_72D0 0xf762575d
// runtime/simd.h:121
#define SF2C_SIMD_N_0X806567CB_5466 0x806567cb
// runtime/simd.h:121
#define SF2C_SIMD_N_0X196C3671_D88B 0x196c3671
// runtime/simd.h:121
#define SF2C_SIMD_N_0X6E6B06E7_CFD5 0x6e6b06e7
// runtime/simd.h:122
#define SF2C_SIMD_N_0XFED41B76_C76E 0xfed41b76
// runtime/simd.h:122
#define SF2C_SIMD_N_0X89D32BE0_DE26 0x89d32be0
// runtime/simd.h:122
#define SF2C_SIMD_N_0X10DA7A5A_0BE2 0x10da7a5a
// runtime/simd.h:122
#define SF2C_SIMD_N_0X67DD4ACC_515A 0x67dd4acc
// runtime/simd.h:122
#define SF2C_SIMD_N_0XF9B9DF6F_6D6B 0xf9b9df6f
// runtime/simd.h:122
#define SF2C_SIMD_N_0X8EBEEFF9_4E4E 0x8ebeeff9
// runtime/simd.h:122
#define SF2C_SIMD_N_0X17B7BE43_F631 0x17b7be43
// runtime/simd.h:122
#define SF2C_SIMD_N_0X60B08ED5_27FE 0x60b08ed5
// runtime/simd.h:123
#define SF2C_SIMD_N_0XD6D6A3E8_8B6D 0xd6d6a3e8
// runtime/simd.h:123
#define SF2C_SIMD_N_0XA1D1937E_22FF 0xa1d1937e
// runtime/simd.h:123
#define SF2C_SIMD_N_0X38D8C2C4_8090 0x38d8c2c4
// runtime/simd.h:123
#define SF2C_SIMD_N_0X4FDFF252_988E 0x4fdff252
// runtime/simd.h:123
#define SF2C_SIMD_N_0XD1BB67F1_647F 0xd1bb67f1
// runtime/simd.h:123
#define SF2C_SIMD_N_0XA6BC5767_A866 0xa6bc5767
// runtime/simd.h:123
#define SF2C_SIMD_N_0X3FB506DD_91D3 0x3fb506dd
// runtime/simd.h:123
#define SF2C_SIMD_N_0X48B2364B_B7CF 0x48b2364b
// runtime/simd.h:124
#define SF2C_SIMD_N_0XD80D2BDA_5486 0xd80d2bda
// runtime/simd.h:124
#define SF2C_SIMD_N_0XAF0A1B4C_11D5 0xaf0a1b4c
// runtime/simd.h:124
#define SF2C_SIMD_N_0X36034AF6_56DE 0x36034af6
// runtime/simd.h:124
#define SF2C_SIMD_N_0X41047A60_C267 0x41047a60
// runtime/simd.h:124
#define SF2C_SIMD_N_0XDF60EFC3_39B3 0xdf60efc3
// runtime/simd.h:124
#define SF2C_SIMD_N_0XA867DF55_BB83 0xa867df55
// runtime/simd.h:124
#define SF2C_SIMD_N_0X316E8EEF_EE27 0x316e8eef
// runtime/simd.h:124
#define SF2C_SIMD_N_0X4669BE79_9553 0x4669be79
// runtime/simd.h:125
#define SF2C_SIMD_N_0XCB61B38C_AFC2 0xcb61b38c
// runtime/simd.h:125
#define SF2C_SIMD_N_0XBC66831A_75E0 0xbc66831a
// runtime/simd.h:125
#define SF2C_SIMD_N_0X256FD2A0_1DF1 0x256fd2a0
// runtime/simd.h:125
#define SF2C_SIMD_N_0X5268E236_2A0D 0x5268e236
// runtime/simd.h:125
#define SF2C_SIMD_N_0XCC0C7795_D263 0xcc0c7795
// runtime/simd.h:125
#define SF2C_SIMD_N_0XBB0B4703_C1FF 0xbb0b4703
// runtime/simd.h:125
#define SF2C_SIMD_N_0X220216B9_FFB3 0x220216b9
// runtime/simd.h:125
#define SF2C_SIMD_N_0X5505262F_84B3 0x5505262f
// runtime/simd.h:126
#define SF2C_SIMD_N_0XC5BA3BBE_4E42 0xc5ba3bbe
// runtime/simd.h:126
#define SF2C_SIMD_N_0XB2BD0B28_138E 0xb2bd0b28
// runtime/simd.h:126
#define SF2C_SIMD_N_0X2BB45A92_C85D 0x2bb45a92
// runtime/simd.h:126
#define SF2C_SIMD_N_0X5CB36A04_237B 0x5cb36a04
// runtime/simd.h:126
#define SF2C_SIMD_N_0XC2D7FFA7_B710 0xc2d7ffa7
// runtime/simd.h:126
#define SF2C_SIMD_N_0XB5D0CF31_B222 0xb5d0cf31
// runtime/simd.h:126
#define SF2C_SIMD_N_0X2CD99E8B_C45C 0x2cd99e8b
// runtime/simd.h:126
#define SF2C_SIMD_N_0X5BDEAE1D_F848 0x5bdeae1d
// runtime/simd.h:127
#define SF2C_SIMD_N_0X9B64C2B0_B1F1 0x9b64c2b0
// runtime/simd.h:127
#define SF2C_SIMD_N_0XEC63F226_03F3 0xec63f226
// runtime/simd.h:127
#define SF2C_SIMD_N_0X756AA39C_EBF7 0x756aa39c
// runtime/simd.h:127
#define SF2C_SIMD_N_0X026D930A_5397 0x026d930a
// runtime/simd.h:127
#define SF2C_SIMD_N_0X9C0906A9_B9BE 0x9c0906a9
// runtime/simd.h:127
#define SF2C_SIMD_N_0XEB0E363F_9DAA 0xeb0e363f
// runtime/simd.h:127
#define SF2C_SIMD_N_0X72076785_E3F4 0x72076785
// runtime/simd.h:127
#define SF2C_SIMD_N_0X05005713_018C 0x05005713
// runtime/simd.h:128
#define SF2C_SIMD_N_0X95BF4A82_CCCE 0x95bf4a82
// runtime/simd.h:128
#define SF2C_SIMD_N_0XE2B87A14_6246 0xe2b87a14
// runtime/simd.h:128
#define SF2C_SIMD_N_0X7BB12BAE_B9B9 0x7bb12bae
// runtime/simd.h:128
#define SF2C_SIMD_N_0X0CB61B38_5311 0x0cb61b38
// runtime/simd.h:128
#define SF2C_SIMD_N_0X92D28E9B_CBFB 0x92d28e9b
// runtime/simd.h:128
#define SF2C_SIMD_N_0XE5D5BE0D_F918 0xe5d5be0d
// runtime/simd.h:128
#define SF2C_SIMD_N_0X7CDCEFB7_9AFC 0x7cdcefb7
// runtime/simd.h:128
#define SF2C_SIMD_N_0X0BDBDF21_22C1 0x0bdbdf21
// runtime/simd.h:129
#define SF2C_SIMD_N_0X86D3D2D4_B211 0x86d3d2d4
// runtime/simd.h:129
#define SF2C_SIMD_N_0XF1D4E242_A11E 0xf1d4e242
// runtime/simd.h:129
#define SF2C_SIMD_N_0X68DDB3F8_C168 0x68ddb3f8
// runtime/simd.h:129
#define SF2C_SIMD_N_0X1FDA836E_B680 0x1fda836e
// runtime/simd.h:129
#define SF2C_SIMD_N_0X81BE16CD_D17F 0x81be16cd
// runtime/simd.h:129
#define SF2C_SIMD_N_0XF6B9265B_F513 0xf6b9265b
// runtime/simd.h:129
#define SF2C_SIMD_N_0X6FB077E1_C3FF 0x6fb077e1
// runtime/simd.h:129
#define SF2C_SIMD_N_0X18B74777_17AC 0x18b74777
// runtime/simd.h:130
#define SF2C_SIMD_N_0X88085AE6_032B 0x88085ae6
// runtime/simd.h:130
#define SF2C_SIMD_N_0XFF0F6A70_204B 0xff0f6a70
// runtime/simd.h:130
#define SF2C_SIMD_N_0X66063BCA_BF75 0x66063bca
// runtime/simd.h:130
#define SF2C_SIMD_N_0X11010B5C_3F9E 0x11010b5c
// runtime/simd.h:130
#define SF2C_SIMD_N_0X8F659EFF_658B 0x8f659eff
// runtime/simd.h:130
#define SF2C_SIMD_N_0XF862AE69_F488 0xf862ae69
// runtime/simd.h:130
#define SF2C_SIMD_N_0X616BFFD3_D613 0x616bffd3
// runtime/simd.h:130
#define SF2C_SIMD_N_0X166CCF45_7AC6 0x166ccf45
// runtime/simd.h:131
#define SF2C_SIMD_N_0XA00AE278_DEC8 0xa00ae278
// runtime/simd.h:131
#define SF2C_SIMD_N_0XD70DD2EE_EBDA 0xd70dd2ee
// runtime/simd.h:131
#define SF2C_SIMD_N_0X4E048354_A6C6 0x4e048354
// runtime/simd.h:131
#define SF2C_SIMD_N_0X3903B3C2_B338 0x3903b3c2
// runtime/simd.h:131
#define SF2C_SIMD_N_0XA7672661_E429 0xa7672661
// runtime/simd.h:131
#define SF2C_SIMD_N_0XD06016F7_E1C8 0xd06016f7
// runtime/simd.h:131
#define SF2C_SIMD_N_0X4969474D_A998 0x4969474d
// runtime/simd.h:131
#define SF2C_SIMD_N_0X3E6E77DB_927A 0x3e6e77db
// runtime/simd.h:132
#define SF2C_SIMD_N_0XAED16A4A_985F 0xaed16a4a
// runtime/simd.h:132
#define SF2C_SIMD_N_0XD9D65ADC_2C02 0xd9d65adc
// runtime/simd.h:132
#define SF2C_SIMD_N_0X40DF0B66_FD09 0x40df0b66
// runtime/simd.h:132
#define SF2C_SIMD_N_0X37D83BF0_D0FC 0x37d83bf0
// runtime/simd.h:132
#define SF2C_SIMD_N_0XA9BCAE53_87BE 0xa9bcae53
// runtime/simd.h:132
#define SF2C_SIMD_N_0XDEBB9EC5_A4D6 0xdebb9ec5
// runtime/simd.h:132
#define SF2C_SIMD_N_0X47B2CF7F_C598 0x47b2cf7f
// runtime/simd.h:132
#define SF2C_SIMD_N_0X30B5FFE9_593E 0x30b5ffe9
// runtime/simd.h:133
#define SF2C_SIMD_N_0XBDBDF21C_B594 0xbdbdf21c
// runtime/simd.h:133
#define SF2C_SIMD_N_0XCABAC28A_E74F 0xcabac28a
// runtime/simd.h:133
#define SF2C_SIMD_N_0X53B39330_C414 0x53b39330
// runtime/simd.h:133
#define SF2C_SIMD_N_0X24B4A3A6_FAB0 0x24b4a3a6
// runtime/simd.h:133
#define SF2C_SIMD_N_0XBAD03605_0648 0xbad03605
// runtime/simd.h:133
#define SF2C_SIMD_N_0XCDD70693_7EF7 0xcdd70693
// runtime/simd.h:133
#define SF2C_SIMD_N_0X54DE5729_8CB7 0x54de5729
// runtime/simd.h:133
#define SF2C_SIMD_N_0X23D967BF_D63D 0x23d967bf
// runtime/simd.h:134
#define SF2C_SIMD_N_0XB3667A2E_5576 0xb3667a2e
// runtime/simd.h:134
#define SF2C_SIMD_N_0XC4614AB8_723D 0xc4614ab8
// runtime/simd.h:134
#define SF2C_SIMD_N_0X5D681B02_701A 0x5d681b02
// runtime/simd.h:134
#define SF2C_SIMD_N_0X2A6F2B94_ADB2 0x2a6f2b94
// runtime/simd.h:134
#define SF2C_SIMD_N_0XB40BBE37_7F9B 0xb40bbe37
// runtime/simd.h:134
#define SF2C_SIMD_N_0XC30C8EA1_55E8 0xc30c8ea1
// runtime/simd.h:134
#define SF2C_SIMD_N_0X5A05DF1B_30F9 0x5a05df1b
// runtime/simd.h:134
#define SF2C_SIMD_N_0X2D02EF8D_EC7E 0x2d02ef8d
// runtime/simd.h:137
#define SF2C_SIMD_N_0XF26B8303_8435 0xf26b8303
// runtime/simd.h:137
#define SF2C_SIMD_N_0XE13B70F7_9A7B 0xe13b70f7
// runtime/simd.h:137
#define SF2C_SIMD_N_0X1350F3F4_965F 0x1350f3f4
// runtime/simd.h:137
#define SF2C_SIMD_N_0XC79A971F_EEC6 0xc79a971f
// runtime/simd.h:137
#define SF2C_SIMD_N_0X35F1141C_08C3 0x35f1141c
// runtime/simd.h:137
#define SF2C_SIMD_N_0X26A1E7E8_6821 0x26a1e7e8
// runtime/simd.h:137
#define SF2C_SIMD_N_0XD4CA64EB_783B 0xd4ca64eb
// runtime/simd.h:138
#define SF2C_SIMD_N_0X8AD958CF_83E4 0x8ad958cf
// runtime/simd.h:138
#define SF2C_SIMD_N_0X78B2DBCC_8AB6 0x78b2dbcc
// runtime/simd.h:138
#define SF2C_SIMD_N_0X6BE22838_E8D3 0x6be22838
// runtime/simd.h:138
#define SF2C_SIMD_N_0X9989AB3B_5417 0x9989ab3b
// runtime/simd.h:138
#define SF2C_SIMD_N_0X4D43CFD0_845D 0x4d43cfd0
// runtime/simd.h:138
#define SF2C_SIMD_N_0XBF284CD3_4058 0xbf284cd3
// runtime/simd.h:138
#define SF2C_SIMD_N_0XAC78BF27_1B15 0xac78bf27
// runtime/simd.h:138
#define SF2C_SIMD_N_0X5E133C24_4FC2 0x5e133c24
// runtime/simd.h:139
#define SF2C_SIMD_N_0X105EC76F_0193 0x105ec76f
// runtime/simd.h:139
#define SF2C_SIMD_N_0XE235446C_7B3C 0xe235446c
// runtime/simd.h:139
#define SF2C_SIMD_N_0XF165B798_57BA 0xf165b798
// runtime/simd.h:139
#define SF2C_SIMD_N_0X030E349B_A922 0x030e349b
// runtime/simd.h:139
#define SF2C_SIMD_N_0XD7C45070_8F3A 0xd7c45070
// runtime/simd.h:139
#define SF2C_SIMD_N_0X25AFD373_0202 0x25afd373
// runtime/simd.h:139
#define SF2C_SIMD_N_0X36FF2087_70AE 0x36ff2087
// runtime/simd.h:139
#define SF2C_SIMD_N_0XC494A384_E1EF 0xc494a384
// runtime/simd.h:140
#define SF2C_SIMD_N_0X9A879FA0_DB14 0x9a879fa0
// runtime/simd.h:140
#define SF2C_SIMD_N_0X68EC1CA3_77F2 0x68ec1ca3
// runtime/simd.h:140
#define SF2C_SIMD_N_0X7BBCEF57_F059 0x7bbcef57
// runtime/simd.h:140
#define SF2C_SIMD_N_0X89D76C54_02A5 0x89d76c54
// runtime/simd.h:140
#define SF2C_SIMD_N_0X5D1D08BF_A4EF 0x5d1d08bf
// runtime/simd.h:140
#define SF2C_SIMD_N_0XAF768BBC_4265 0xaf768bbc
// runtime/simd.h:140
#define SF2C_SIMD_N_0XBC267848_765E 0xbc267848
// runtime/simd.h:140
#define SF2C_SIMD_N_0X4E4DFB4B_8EC4 0x4e4dfb4b
// runtime/simd.h:141
#define SF2C_SIMD_N_0X20BD8EDE_671A 0x20bd8ede
// runtime/simd.h:141
#define SF2C_SIMD_N_0XD2D60DDD_6A47 0xd2d60ddd
// runtime/simd.h:141
#define SF2C_SIMD_N_0XC186FE29_EB27 0xc186fe29
// runtime/simd.h:141
#define SF2C_SIMD_N_0X33ED7D2A_EE28 0x33ed7d2a
// runtime/simd.h:141
#define SF2C_SIMD_N_0XE72719C1_398D 0xe72719c1
// runtime/simd.h:141
#define SF2C_SIMD_N_0X154C9AC2_848B 0x154c9ac2
// runtime/simd.h:141
#define SF2C_SIMD_N_0X061C6936_5092 0x061c6936
// runtime/simd.h:141
#define SF2C_SIMD_N_0XF477EA35_30E3 0xf477ea35
// runtime/simd.h:142
#define SF2C_SIMD_N_0XAA64D611_C431 0xaa64d611
// runtime/simd.h:142
#define SF2C_SIMD_N_0X580F5512_144F 0x580f5512
// runtime/simd.h:142
#define SF2C_SIMD_N_0X4B5FA6E6_54D2 0x4b5fa6e6
// runtime/simd.h:142
#define SF2C_SIMD_N_0XB93425E5_EA06 0xb93425e5
// runtime/simd.h:142
#define SF2C_SIMD_N_0X6DFE410E_9A1C 0x6dfe410e
// runtime/simd.h:142
#define SF2C_SIMD_N_0X9F95C20D_9EC7 0x9f95c20d
// runtime/simd.h:142
#define SF2C_SIMD_N_0X8CC531F9_68F6 0x8cc531f9
// runtime/simd.h:142
#define SF2C_SIMD_N_0X7EAEB2FA_515F 0x7eaeb2fa
// runtime/simd.h:143
#define SF2C_SIMD_N_0X30E349B1_C361 0x30e349b1
// runtime/simd.h:143
#define SF2C_SIMD_N_0XC288CAB2_A8D5 0xc288cab2
// runtime/simd.h:143
#define SF2C_SIMD_N_0XD1D83946_587C 0xd1d83946
// runtime/simd.h:143
#define SF2C_SIMD_N_0X23B3BA45_7D85 0x23b3ba45
// runtime/simd.h:143
#define SF2C_SIMD_N_0XF779DEAE_A025 0xf779deae
// runtime/simd.h:143
#define SF2C_SIMD_N_0X05125DAD_DFA8 0x05125dad
// runtime/simd.h:143
#define SF2C_SIMD_N_0X1642AE59_9CA4 0x1642ae59
// runtime/simd.h:143
#define SF2C_SIMD_N_0XE4292D5A_B2BB 0xe4292d5a
// runtime/simd.h:144
#define SF2C_SIMD_N_0XBA3A117E_88A9 0xba3a117e
// runtime/simd.h:144
#define SF2C_SIMD_N_0X4851927D_1239 0x4851927d
// runtime/simd.h:144
#define SF2C_SIMD_N_0X5B016189_90C0 0x5b016189
// runtime/simd.h:144
#define SF2C_SIMD_N_0XA96AE28A_59EF 0xa96ae28a
// runtime/simd.h:144
#define SF2C_SIMD_N_0X7DA08661_4F02 0x7da08661
// runtime/simd.h:144
#define SF2C_SIMD_N_0X8FCB0562_6206 0x8fcb0562
// runtime/simd.h:144
#define SF2C_SIMD_N_0X9C9BF696_5B8C 0x9c9bf696
// runtime/simd.h:144
#define SF2C_SIMD_N_0X6EF07595_04F9 0x6ef07595
// runtime/simd.h:145
#define SF2C_SIMD_N_0X417B1DBC_1FC9 0x417b1dbc
// runtime/simd.h:145
#define SF2C_SIMD_N_0XB3109EBF_42A8 0xb3109ebf
// runtime/simd.h:145
#define SF2C_SIMD_N_0XA0406D4B_636E 0xa0406d4b
// runtime/simd.h:145
#define SF2C_SIMD_N_0X522BEE48_F80B 0x522bee48
// runtime/simd.h:145
#define SF2C_SIMD_N_0X86E18AA3_518D 0x86e18aa3
// runtime/simd.h:145
#define SF2C_SIMD_N_0X748A09A0_458B 0x748a09a0
// runtime/simd.h:145
#define SF2C_SIMD_N_0X67DAFA54_CD14 0x67dafa54
// runtime/simd.h:145
#define SF2C_SIMD_N_0X95B17957_B891 0x95b17957
// runtime/simd.h:146
#define SF2C_SIMD_N_0XCBA24573_0254 0xcba24573
// runtime/simd.h:146
#define SF2C_SIMD_N_0X39C9C670_413E 0x39c9c670
// runtime/simd.h:146
#define SF2C_SIMD_N_0X2A993584_CCE3 0x2a993584
// runtime/simd.h:146
#define SF2C_SIMD_N_0XD8F2B687_9FCF 0xd8f2b687
// runtime/simd.h:146
#define SF2C_SIMD_N_0X0C38D26C_6173 0x0c38d26c
// runtime/simd.h:146
#define SF2C_SIMD_N_0XFE53516F_1FB3 0xfe53516f
// runtime/simd.h:146
#define SF2C_SIMD_N_0XED03A29B_3543 0xed03a29b
// runtime/simd.h:146
#define SF2C_SIMD_N_0X1F682198_F637 0x1f682198
// runtime/simd.h:147
#define SF2C_SIMD_N_0X5125DAD3_CC0A 0x5125dad3
// runtime/simd.h:147
#define SF2C_SIMD_N_0XA34E59D0_0A9D 0xa34e59d0
// runtime/simd.h:147
#define SF2C_SIMD_N_0XB01EAA24_89AA 0xb01eaa24
// runtime/simd.h:147
#define SF2C_SIMD_N_0X42752927_0530 0x42752927
// runtime/simd.h:147
#define SF2C_SIMD_N_0X96BF4DCC_4432 0x96bf4dcc
// runtime/simd.h:147
#define SF2C_SIMD_N_0X64D4CECF_C24C 0x64d4cecf
// runtime/simd.h:147
#define SF2C_SIMD_N_0X77843D3B_DD3C 0x77843d3b
// runtime/simd.h:147
#define SF2C_SIMD_N_0X85EFBE38_E1ED 0x85efbe38
// runtime/simd.h:148
#define SF2C_SIMD_N_0XDBFC821C_7E4F 0xdbfc821c
// runtime/simd.h:148
#define SF2C_SIMD_N_0X2997011F_590B 0x2997011f
// runtime/simd.h:148
#define SF2C_SIMD_N_0X3AC7F2EB_CCEB 0x3ac7f2eb
// runtime/simd.h:148
#define SF2C_SIMD_N_0XC8AC71E8_2FDA 0xc8ac71e8
// runtime/simd.h:148
#define SF2C_SIMD_N_0X1C661503_CF8F 0x1c661503
// runtime/simd.h:148
#define SF2C_SIMD_N_0XEE0D9600_37F3 0xee0d9600
// runtime/simd.h:148
#define SF2C_SIMD_N_0XFD5D65F4_1A70 0xfd5d65f4
// runtime/simd.h:148
#define SF2C_SIMD_N_0X0F36E6F7_1293 0x0f36e6f7
// runtime/simd.h:149
#define SF2C_SIMD_N_0X61C69362_B8AE 0x61c69362
// runtime/simd.h:149
#define SF2C_SIMD_N_0X93AD1061_1666 0x93ad1061
// runtime/simd.h:149
#define SF2C_SIMD_N_0X80FDE395_6D3F 0x80fde395
// runtime/simd.h:149
#define SF2C_SIMD_N_0X72966096_7E4D 0x72966096
// runtime/simd.h:149
#define SF2C_SIMD_N_0XA65C047D_C8C0 0xa65c047d
// runtime/simd.h:149
#define SF2C_SIMD_N_0X5437877E_C37A 0x5437877e
// runtime/simd.h:149
#define SF2C_SIMD_N_0X4767748A_9E5F 0x4767748a
// runtime/simd.h:149
#define SF2C_SIMD_N_0XB50CF789_C9B3 0xb50cf789
// runtime/simd.h:150
#define SF2C_SIMD_N_0XEB1FCBAD_CC28 0xeb1fcbad
// runtime/simd.h:150
#define SF2C_SIMD_N_0X197448AE_9889 0x197448ae
// runtime/simd.h:150
#define SF2C_SIMD_N_0X0A24BB5A_1560 0x0a24bb5a
// runtime/simd.h:150
#define SF2C_SIMD_N_0XF84F3859_26EB 0xf84f3859
// runtime/simd.h:150
#define SF2C_SIMD_N_0X2C855CB2_64EA 0x2c855cb2
// runtime/simd.h:150
#define SF2C_SIMD_N_0XDEEEDFB1_313F 0xdeeedfb1
// runtime/simd.h:150
#define SF2C_SIMD_N_0XCDBE2C45_0F92 0xcdbe2c45
// runtime/simd.h:150
#define SF2C_SIMD_N_0X3FD5AF46_B1C2 0x3fd5af46
// runtime/simd.h:151
#define SF2C_SIMD_N_0X7198540D_5791 0x7198540d
// runtime/simd.h:151
#define SF2C_SIMD_N_0X83F3D70E_64FB 0x83f3d70e
// runtime/simd.h:151
#define SF2C_SIMD_N_0X90A324FA_F272 0x90a324fa
// runtime/simd.h:151
#define SF2C_SIMD_N_0X62C8A7F9_CD74 0x62c8a7f9
// runtime/simd.h:151
#define SF2C_SIMD_N_0XB602C312_B654 0xb602c312
// runtime/simd.h:151
#define SF2C_SIMD_N_0X44694011_C197 0x44694011
// runtime/simd.h:151
#define SF2C_SIMD_N_0X5739B3E5_26E0 0x5739b3e5
// runtime/simd.h:151
#define SF2C_SIMD_N_0XA55230E6_FDE9 0xa55230e6
// runtime/simd.h:152
#define SF2C_SIMD_N_0XFB410CC2_897A 0xfb410cc2
// runtime/simd.h:152
#define SF2C_SIMD_N_0X092A8FC1_D768 0x092a8fc1
// runtime/simd.h:152
#define SF2C_SIMD_N_0X1A7A7C35_6D1F 0x1a7a7c35
// runtime/simd.h:152
#define SF2C_SIMD_N_0XE811FF36_2C4C 0xe811ff36
// runtime/simd.h:152
#define SF2C_SIMD_N_0X3CDB9BDD_D998 0x3cdb9bdd
// runtime/simd.h:152
#define SF2C_SIMD_N_0XCEB018DE_2DA1 0xceb018de
// runtime/simd.h:152
#define SF2C_SIMD_N_0XDDE0EB2A_4DF7 0xdde0eb2a
// runtime/simd.h:152
#define SF2C_SIMD_N_0X2F8B6829_11F4 0x2f8b6829
// runtime/simd.h:153
#define SF2C_SIMD_N_0X82F63B78_70DC 0x82f63b78
// runtime/simd.h:153
#define SF2C_SIMD_N_0X709DB87B_FE98 0x709db87b
// runtime/simd.h:153
#define SF2C_SIMD_N_0X63CD4B8F_B49E 0x63cd4b8f
// runtime/simd.h:153
#define SF2C_SIMD_N_0X91A6C88C_B3B0 0x91a6c88c
// runtime/simd.h:153
#define SF2C_SIMD_N_0X456CAC67_7E9F 0x456cac67
// runtime/simd.h:153
#define SF2C_SIMD_N_0XB7072F64_8073 0xb7072f64
// runtime/simd.h:153
#define SF2C_SIMD_N_0XA457DC90_DF3F 0xa457dc90
// runtime/simd.h:153
#define SF2C_SIMD_N_0X563C5F93_1C30 0x563c5f93
// runtime/simd.h:154
#define SF2C_SIMD_N_0X082F63B7_33B0 0x082f63b7
// runtime/simd.h:154
#define SF2C_SIMD_N_0XFA44E0B4_413D 0xfa44e0b4
// runtime/simd.h:154
#define SF2C_SIMD_N_0XE9141340_4FB5 0xe9141340
// runtime/simd.h:154
#define SF2C_SIMD_N_0X1B7F9043_B260 0x1b7f9043
// runtime/simd.h:154
#define SF2C_SIMD_N_0XCFB5F4A8_FBE7 0xcfb5f4a8
// runtime/simd.h:154
#define SF2C_SIMD_N_0X3DDE77AB_EAC3 0x3dde77ab
// runtime/simd.h:154
#define SF2C_SIMD_N_0X2E8E845F_221A 0x2e8e845f
// runtime/simd.h:154
#define SF2C_SIMD_N_0XDCE5075C_B055 0xdce5075c
// runtime/simd.h:155
#define SF2C_SIMD_N_0X92A8FC17_7EE6 0x92a8fc17
// runtime/simd.h:155
#define SF2C_SIMD_N_0X60C37F14_241E 0x60c37f14
// runtime/simd.h:155
#define SF2C_SIMD_N_0X73938CE0_41C7 0x73938ce0
// runtime/simd.h:155
#define SF2C_SIMD_N_0X81F80FE3_D298 0x81f80fe3
// runtime/simd.h:155
#define SF2C_SIMD_N_0X55326B08_DB7A 0x55326b08
// runtime/simd.h:155
#define SF2C_SIMD_N_0XA759E80B_4254 0xa759e80b
// runtime/simd.h:155
#define SF2C_SIMD_N_0XB4091BFF_F86D 0xb4091bff
// runtime/simd.h:155
#define SF2C_SIMD_N_0X466298FC_835F 0x466298fc
// runtime/simd.h:156
#define SF2C_SIMD_N_0X1871A4D8_F2C0 0x1871a4d8
// runtime/simd.h:156
#define SF2C_SIMD_N_0XEA1A27DB_DE10 0xea1a27db
// runtime/simd.h:156
#define SF2C_SIMD_N_0XF94AD42F_FC8A 0xf94ad42f
// runtime/simd.h:156
#define SF2C_SIMD_N_0X0B21572C_936F 0x0b21572c
// runtime/simd.h:156
#define SF2C_SIMD_N_0XDFEB33C7_99BA 0xdfeb33c7
// runtime/simd.h:156
#define SF2C_SIMD_N_0X2D80B0C4_DBFD 0x2d80b0c4
// runtime/simd.h:156
#define SF2C_SIMD_N_0X3ED04330_6171 0x3ed04330
// runtime/simd.h:156
#define SF2C_SIMD_N_0XCCBBC033_689C 0xccbbc033
// runtime/simd.h:157
#define SF2C_SIMD_N_0XA24BB5A6_0BF3 0xa24bb5a6
// runtime/simd.h:157
#define SF2C_SIMD_N_0X502036A5_CD2E 0x502036a5
// runtime/simd.h:157
#define SF2C_SIMD_N_0X4370C551_2434 0x4370c551
// runtime/simd.h:157
#define SF2C_SIMD_N_0XB11B4652_44FE 0xb11b4652
// runtime/simd.h:157
#define SF2C_SIMD_N_0X65D122B9_0A78 0x65d122b9
// runtime/simd.h:157
#define SF2C_SIMD_N_0X97BAA1BA_632D 0x97baa1ba
// runtime/simd.h:157
#define SF2C_SIMD_N_0X84EA524E_EF50 0x84ea524e
// runtime/simd.h:157
#define SF2C_SIMD_N_0X7681D14D_1154 0x7681d14d
// runtime/simd.h:158
#define SF2C_SIMD_N_0X2892ED69_F00F 0x2892ed69
// runtime/simd.h:158
#define SF2C_SIMD_N_0XDAF96E6A_E1DA 0xdaf96e6a
// runtime/simd.h:158
#define SF2C_SIMD_N_0XC9A99D9E_9C62 0xc9a99d9e
// runtime/simd.h:158
#define SF2C_SIMD_N_0X3BC21E9D_F70C 0x3bc21e9d
// runtime/simd.h:158
#define SF2C_SIMD_N_0XEF087A76_42C3 0xef087a76
// runtime/simd.h:158
#define SF2C_SIMD_N_0X1D63F975_0A16 0x1d63f975
// runtime/simd.h:158
#define SF2C_SIMD_N_0X0E330A81_34EC 0x0e330a81
// runtime/simd.h:158
#define SF2C_SIMD_N_0XFC588982_A863 0xfc588982
// runtime/simd.h:159
#define SF2C_SIMD_N_0XB21572C9_37F3 0xb21572c9
// runtime/simd.h:159
#define SF2C_SIMD_N_0X407EF1CA_DF53 0x407ef1ca
// runtime/simd.h:159
#define SF2C_SIMD_N_0X532E023E_465A 0x532e023e
// runtime/simd.h:159
#define SF2C_SIMD_N_0XA145813D_8A8D 0xa145813d
// runtime/simd.h:159
#define SF2C_SIMD_N_0X758FE5D6_2F42 0x758fe5d6
// runtime/simd.h:159
#define SF2C_SIMD_N_0X87E466D5_009D 0x87e466d5
// runtime/simd.h:159
#define SF2C_SIMD_N_0X94B49521_B419 0x94b49521
// runtime/simd.h:159
#define SF2C_SIMD_N_0X66DF1622_D65C 0x66df1622
// runtime/simd.h:160
#define SF2C_SIMD_N_0X38CC2A06_E25C 0x38cc2a06
// runtime/simd.h:160
#define SF2C_SIMD_N_0XCAA7A905_FDB7 0xcaa7a905
// runtime/simd.h:160
#define SF2C_SIMD_N_0XD9F75AF1_4B34 0xd9f75af1
// runtime/simd.h:160
#define SF2C_SIMD_N_0X2B9CD9F2_D13B 0x2b9cd9f2
// runtime/simd.h:160
#define SF2C_SIMD_N_0XFF56BD19_823A 0xff56bd19
// runtime/simd.h:160
#define SF2C_SIMD_N_0X0D3D3E1A_F5AA 0x0d3d3e1a
// runtime/simd.h:160
#define SF2C_SIMD_N_0X1E6DCDEE_DE6E 0x1e6dcdee
// runtime/simd.h:160
#define SF2C_SIMD_N_0XEC064EED_E2F2 0xec064eed
// runtime/simd.h:161
#define SF2C_SIMD_N_0XC38D26C4_73BB 0xc38d26c4
// runtime/simd.h:161
#define SF2C_SIMD_N_0X31E6A5C7_D5BD 0x31e6a5c7
// runtime/simd.h:161
#define SF2C_SIMD_N_0X22B65633_8978 0x22b65633
// runtime/simd.h:161
#define SF2C_SIMD_N_0XD0DDD530_B0DC 0xd0ddd530
// runtime/simd.h:161
#define SF2C_SIMD_N_0X0417B1DB_62DA 0x0417b1db
// runtime/simd.h:161
#define SF2C_SIMD_N_0XF67C32D8_AB95 0xf67c32d8
// runtime/simd.h:161
#define SF2C_SIMD_N_0XE52CC12C_EFE1 0xe52cc12c
// runtime/simd.h:161
#define SF2C_SIMD_N_0X1747422F_9392 0x1747422f
// runtime/simd.h:162
#define SF2C_SIMD_N_0X49547E0B_6895 0x49547e0b
// runtime/simd.h:162
#define SF2C_SIMD_N_0XBB3FFD08_99C2 0xbb3ffd08
// runtime/simd.h:162
#define SF2C_SIMD_N_0XA86F0EFC_2544 0xa86f0efc
// runtime/simd.h:162
#define SF2C_SIMD_N_0X5A048DFF_A3D4 0x5a048dff
// runtime/simd.h:162
#define SF2C_SIMD_N_0X8ECEE914_4404 0x8ecee914
// runtime/simd.h:162
#define SF2C_SIMD_N_0X7CA56A17_8F8F 0x7ca56a17
// runtime/simd.h:162
#define SF2C_SIMD_N_0X6FF599E3_B681 0x6ff599e3
// runtime/simd.h:162
#define SF2C_SIMD_N_0X9D9E1AE0_8391 0x9d9e1ae0
// runtime/simd.h:163
#define SF2C_SIMD_N_0XD3D3E1AB_5BAD 0xd3d3e1ab
// runtime/simd.h:163
#define SF2C_SIMD_N_0X21B862A8_5684 0x21b862a8
// runtime/simd.h:163
#define SF2C_SIMD_N_0X32E8915C_0680 0x32e8915c
// runtime/simd.h:163
#define SF2C_SIMD_N_0XC083125F_ED9C 0xc083125f
// runtime/simd.h:163
#define SF2C_SIMD_N_0X144976B4_E765 0x144976b4
// runtime/simd.h:163
#define SF2C_SIMD_N_0XE622F5B7_D4BB 0xe622f5b7
// runtime/simd.h:163
#define SF2C_SIMD_N_0XF5720643_A9BE 0xf5720643
// runtime/simd.h:163
#define SF2C_SIMD_N_0X07198540_5C0C 0x07198540
// runtime/simd.h:164
#define SF2C_SIMD_N_0X590AB964_E9EB 0x590ab964
// runtime/simd.h:164
#define SF2C_SIMD_N_0XAB613A67_17D2 0xab613a67
// runtime/simd.h:164
#define SF2C_SIMD_N_0XB831C993_8944 0xb831c993
// runtime/simd.h:164
#define SF2C_SIMD_N_0X4A5A4A90_A15D 0x4a5a4a90
// runtime/simd.h:164
#define SF2C_SIMD_N_0X9E902E7B_D625 0x9e902e7b
// runtime/simd.h:164
#define SF2C_SIMD_N_0X6CFBAD78_1BB1 0x6cfbad78
// runtime/simd.h:164
#define SF2C_SIMD_N_0X7FAB5E8C_346A 0x7fab5e8c
// runtime/simd.h:164
#define SF2C_SIMD_N_0X8DC0DD8F_44F4 0x8dc0dd8f
// runtime/simd.h:165
#define SF2C_SIMD_N_0XE330A81A_1522 0xe330a81a
// runtime/simd.h:165
#define SF2C_SIMD_N_0X115B2B19_E4A4 0x115b2b19
// runtime/simd.h:165
#define SF2C_SIMD_N_0X020BD8ED_50A7 0x020bd8ed
// runtime/simd.h:165
#define SF2C_SIMD_N_0XF0605BEE_66DB 0xf0605bee
// runtime/simd.h:165
#define SF2C_SIMD_N_0X24AA3F05_2CDD 0x24aa3f05
// runtime/simd.h:165
#define SF2C_SIMD_N_0XD6C1BC06_B1D7 0xd6c1bc06
// runtime/simd.h:165
#define SF2C_SIMD_N_0XC5914FF2_1474 0xc5914ff2
// runtime/simd.h:165
#define SF2C_SIMD_N_0X37FACCF1_F829 0x37faccf1
// runtime/simd.h:166
#define SF2C_SIMD_N_0X69E9F0D5_AA8A 0x69e9f0d5
// runtime/simd.h:166
#define SF2C_SIMD_N_0X9B8273D6_1FDD 0x9b8273d6
// runtime/simd.h:166
#define SF2C_SIMD_N_0X88D28022_E2C3 0x88d28022
// runtime/simd.h:166
#define SF2C_SIMD_N_0X7AB90321_547F 0x7ab90321
// runtime/simd.h:166
#define SF2C_SIMD_N_0XAE7367CA_A043 0xae7367ca
// runtime/simd.h:166
#define SF2C_SIMD_N_0X5C18E4C9_D21A 0x5c18e4c9
// runtime/simd.h:166
#define SF2C_SIMD_N_0X4F48173D_0374 0x4f48173d
// runtime/simd.h:166
#define SF2C_SIMD_N_0XBD23943E_5F4B 0xbd23943e
// runtime/simd.h:167
#define SF2C_SIMD_N_0XF36E6F75_558C 0xf36e6f75
// runtime/simd.h:167
#define SF2C_SIMD_N_0X0105EC76_40E9 0x0105ec76
// runtime/simd.h:167
#define SF2C_SIMD_N_0X12551F82_03A9 0x12551f82
// runtime/simd.h:167
#define SF2C_SIMD_N_0XE03E9C81_1950 0xe03e9c81
// runtime/simd.h:167
#define SF2C_SIMD_N_0X34F4F86A_3B4E 0x34f4f86a
// runtime/simd.h:167
#define SF2C_SIMD_N_0XC69F7B69_2B6E 0xc69f7b69
// runtime/simd.h:167
#define SF2C_SIMD_N_0XD5CF889D_6837 0xd5cf889d
// runtime/simd.h:167
#define SF2C_SIMD_N_0X27A40B9E_7B20 0x27a40b9e
// runtime/simd.h:168
#define SF2C_SIMD_N_0X79B737BA_0FB1 0x79b737ba
// runtime/simd.h:168
#define SF2C_SIMD_N_0X8BDCB4B9_9436 0x8bdcb4b9
// runtime/simd.h:168
#define SF2C_SIMD_N_0X988C474D_7B5C 0x988c474d
// runtime/simd.h:168
#define SF2C_SIMD_N_0X6AE7C44E_3280 0x6ae7c44e
// runtime/simd.h:168
#define SF2C_SIMD_N_0XBE2DA0A5_24E5 0xbe2da0a5
// runtime/simd.h:168
#define SF2C_SIMD_N_0X4C4623A6_27C6 0x4c4623a6
// runtime/simd.h:168
#define SF2C_SIMD_N_0X5F16D052_0933 0x5f16d052
// runtime/simd.h:168
#define SF2C_SIMD_N_0XAD7D5351_8A9B 0xad7d5351
// runtime/simd.h:171
#define SF2C_SIMD_N_0X63_A6C6 0x63
// runtime/simd.h:171
#define SF2C_SIMD_N_0X7C_3CC4 0x7c
// runtime/simd.h:171
#define SF2C_SIMD_N_0X77_616A 0x77
// runtime/simd.h:171
#define SF2C_SIMD_N_0X7B_079A 0x7b
// runtime/simd.h:171
#define SF2C_SIMD_N_0XF2_6369 0xf2
// runtime/simd.h:171
#define SF2C_SIMD_N_0X6B_AB8F 0x6b
// runtime/simd.h:171
#define SF2C_SIMD_N_0X6F_F59F 0x6f
// runtime/simd.h:171
#define SF2C_SIMD_N_0XC5_C52F 0xc5
// runtime/simd.h:171
#define SF2C_SIMD_N_0X30_8BAD 0x30
// runtime/simd.h:171
#define SF2C_SIMD_N_0X01_B51C 0x01
// runtime/simd.h:171
#define SF2C_SIMD_N_0X67_6AE1 0x67
// runtime/simd.h:171
#define SF2C_SIMD_N_0X2B_8D3F 0x2b
// runtime/simd.h:171
#define SF2C_SIMD_N_0XFE_C772 0xfe
// runtime/simd.h:171
#define SF2C_SIMD_N_0XD7_8AED 0xd7
// runtime/simd.h:171
#define SF2C_SIMD_N_0XAB_A503 0xab
// runtime/simd.h:171
#define SF2C_SIMD_N_0X76_3ABA 0x76
// runtime/simd.h:172
#define SF2C_SIMD_N_0XCA_AE4D 0xca
// runtime/simd.h:172
#define SF2C_SIMD_N_0X82_09A5 0x82
// runtime/simd.h:172
#define SF2C_SIMD_N_0XC9_EA90 0xc9
// runtime/simd.h:172
#define SF2C_SIMD_N_0X7D_C716 0x7d
// runtime/simd.h:172
#define SF2C_SIMD_N_0XFA_6F06 0xfa
// runtime/simd.h:172
#define SF2C_SIMD_N_0X59_AEEF 0x59
// runtime/simd.h:172
#define SF2C_SIMD_N_0X47_F559 0x47
// runtime/simd.h:172
#define SF2C_SIMD_N_0XF0_2A69 0xf0
// runtime/simd.h:172
#define SF2C_SIMD_N_0XAD_650E 0xad
// runtime/simd.h:172
#define SF2C_SIMD_N_0XD4_6DA2 0xd4
// runtime/simd.h:172
#define SF2C_SIMD_N_0XA2_1A9B 0xa2
// runtime/simd.h:172
#define SF2C_SIMD_N_0XAF_37B9 0xaf
// runtime/simd.h:172
#define SF2C_SIMD_N_0X9C_DEDB 0x9c
// runtime/simd.h:172
#define SF2C_SIMD_N_0XA4_7022 0xa4
// runtime/simd.h:172
#define SF2C_SIMD_N_0X72_C78D 0x72
// runtime/simd.h:172
#define SF2C_SIMD_N_0XC0_4E7B 0xc0
// runtime/simd.h:173
#define SF2C_SIMD_N_0XB7_A515 0xb7
// runtime/simd.h:173
#define SF2C_SIMD_N_0XFD_D1F7 0xfd
// runtime/simd.h:173
#define SF2C_SIMD_N_0X93_ED86 0x93
// runtime/simd.h:173
#define SF2C_SIMD_N_0X26_50D1 0x26
// runtime/simd.h:173
#define SF2C_SIMD_N_0X36_BBAE 0x36
// runtime/simd.h:173
#define SF2C_SIMD_N_0X3F_AEBF 0x3f
// runtime/simd.h:173
#define SF2C_SIMD_N_0XF7_2747 0xf7
// runtime/simd.h:173
#define SF2C_SIMD_N_0XCC_2233 0xcc
// runtime/simd.h:173
#define SF2C_SIMD_N_0X34_806E 0x34
// runtime/simd.h:173
#define SF2C_SIMD_N_0XA5_4926 0xa5
// runtime/simd.h:173
#define SF2C_SIMD_N_0XE5_D81F 0xe5
// runtime/simd.h:173
#define SF2C_SIMD_N_0XF1_2360 0xf1
// runtime/simd.h:173
#define SF2C_SIMD_N_0X71_BFAA 0x71
// runtime/simd.h:173
#define SF2C_SIMD_N_0XD8_8DD9 0xd8
// runtime/simd.h:173
#define SF2C_SIMD_N_0X31_9121 0x31
// runtime/simd.h:173
#define SF2C_SIMD_N_0X15_5FB5 0x15
// runtime/simd.h:174
#define SF2C_SIMD_N_0X04_B03A 0x04
// runtime/simd.h:174
#define SF2C_SIMD_N_0XC7_5A10 0xc7
// runtime/simd.h:174
#define SF2C_SIMD_N_0X23_A029 0x23
// runtime/simd.h:174
#define SF2C_SIMD_N_0XC3_F73F 0xc3
// runtime/simd.h:174
#define SF2C_SIMD_N_0X18_D47C 0x18
// runtime/simd.h:174
#define SF2C_SIMD_N_0X96_14FD 0x96
// runtime/simd.h:174
#define SF2C_SIMD_N_0X05_4ED3 0x05
// runtime/simd.h:174
#define SF2C_SIMD_N_0X9A_D0C7 0x9a
// runtime/simd.h:174
#define SF2C_SIMD_N_0X07_A022 0x07
// runtime/simd.h:174
#define SF2C_SIMD_N_0X12_9AE3 0x12
// runtime/simd.h:174
#define SF2C_SIMD_N_0X80_D41C 0x80
// runtime/simd.h:174
#define SF2C_SIMD_N_0XE2_C924 0xe2
// runtime/simd.h:174
#define SF2C_SIMD_N_0XEB_BCCE 0xeb
// runtime/simd.h:174
#define SF2C_SIMD_N_0X27_D7EC 0x27
// runtime/simd.h:174
#define SF2C_SIMD_N_0XB2_A1F6 0xb2
// runtime/simd.h:174
#define SF2C_SIMD_N_0X75_EF52 0x75
// runtime/simd.h:175
#define SF2C_SIMD_N_0X09_59B7 0x09
// runtime/simd.h:175
#define SF2C_SIMD_N_0X83_7230 0x83
// runtime/simd.h:175
#define SF2C_SIMD_N_0X2C_059D 0x2c
// runtime/simd.h:175
#define SF2C_SIMD_N_0X1A_D752 0x1a
// runtime/simd.h:175
#define SF2C_SIMD_N_0X1B_8C0E 0x1b
// runtime/simd.h:175
#define SF2C_SIMD_N_0X6E_D920 0x6e
// runtime/simd.h:175
#define SF2C_SIMD_N_0X5A_A42C 0x5a
// runtime/simd.h:175
#define SF2C_SIMD_N_0XA0_3862 0xa0
// runtime/simd.h:175
#define SF2C_SIMD_N_0X52_EE58 0x52
// runtime/simd.h:175
#define SF2C_SIMD_N_0X3B_B7AE 0x3b
// runtime/simd.h:175
#define SF2C_SIMD_N_0XD6_AD92 0xd6
// runtime/simd.h:175
#define SF2C_SIMD_N_0XB3_FF48 0xb3
// runtime/simd.h:175
#define SF2C_SIMD_N_0X29_F941 0x29
// runtime/simd.h:175
#define SF2C_SIMD_N_0XE3_614C 0xe3
// runtime/simd.h:175
#define SF2C_SIMD_N_0X2F_435A 0x2f
// runtime/simd.h:175
#define SF2C_SIMD_N_0X84_7CD7 0x84
// runtime/simd.h:176
#define SF2C_SIMD_N_0X53_EF83 0x53
// runtime/simd.h:176
#define SF2C_SIMD_N_0XD1_2170 0xd1
// runtime/simd.h:176
#define SF2C_SIMD_N_0X00_C4DD 0x00
// runtime/simd.h:176
#define SF2C_SIMD_N_0XED_741B 0xed
// runtime/simd.h:176
#define SF2C_SIMD_N_0X20_A766 0x20
// runtime/simd.h:176
#define SF2C_SIMD_N_0XFC_04D6 0xfc
// runtime/simd.h:176
#define SF2C_SIMD_N_0XB1_40FD 0xb1
// runtime/simd.h:176
#define SF2C_SIMD_N_0X5B_5E87 0x5b
// runtime/simd.h:176
#define SF2C_SIMD_N_0X6A_EB3E 0x6a
// runtime/simd.h:176
#define SF2C_SIMD_N_0XCB_45E1 0xcb
// runtime/simd.h:176
#define SF2C_SIMD_N_0XBE_E10E 0xbe
// runtime/simd.h:176
#define SF2C_SIMD_N_0X39_F666 0x39
// runtime/simd.h:176
#define SF2C_SIMD_N_0X4A_D10C 0x4a
// runtime/simd.h:176
#define SF2C_SIMD_N_0X4C_C1BA 0x4c
// runtime/simd.h:176
#define SF2C_SIMD_N_0X58_08FB 0x58
// runtime/simd.h:176
#define SF2C_SIMD_N_0XCF_1D5E 0xcf
// runtime/simd.h:177
#define SF2C_SIMD_N_0XD0_42B7 0xd0
// runtime/simd.h:177
#define SF2C_SIMD_N_0XEF_8D17 0xef
// runtime/simd.h:177
#define SF2C_SIMD_N_0XAA_6730 0xaa
// runtime/simd.h:177
#define SF2C_SIMD_N_0XFB_1295 0xfb
// runtime/simd.h:177
#define SF2C_SIMD_N_0X43_6CE1 0x43
// runtime/simd.h:177
#define SF2C_SIMD_N_0X4D_3BDF 0x4d
// runtime/simd.h:177
#define SF2C_SIMD_N_0X33_8FFB 0x33
// runtime/simd.h:177
#define SF2C_SIMD_N_0X85_124B 0x85
// runtime/simd.h:177
#define SF2C_SIMD_N_0X45_17A0 0x45
// runtime/simd.h:177
#define SF2C_SIMD_N_0XF9_9E69 0xf9
// runtime/simd.h:177
#define SF2C_SIMD_N_0X02_E6BD 0x02
// runtime/simd.h:177
#define SF2C_SIMD_N_0X7F_0AFB 0x7f
// runtime/simd.h:177
#define SF2C_SIMD_N_0X50_4171 0x50
// runtime/simd.h:177
#define SF2C_SIMD_N_0X3C_4150 0x3c
// runtime/simd.h:177
#define SF2C_SIMD_N_0X9F_0249 0x9f
// runtime/simd.h:177
#define SF2C_SIMD_N_0XA8_D409 0xa8
// runtime/simd.h:178
#define SF2C_SIMD_N_0X51_E75E 0x51
// runtime/simd.h:178
#define SF2C_SIMD_N_0XA3_20B4 0xa3
// runtime/simd.h:178
#define SF2C_SIMD_N_0X40_1AF3 0x40
// runtime/simd.h:178
#define SF2C_SIMD_N_0X8F_CF33 0x8f
// runtime/simd.h:178
#define SF2C_SIMD_N_0X92_E9B0 0x92
// runtime/simd.h:178
#define SF2C_SIMD_N_0X9D_4109 0x9d
// runtime/simd.h:178
#define SF2C_SIMD_N_0X38_AE4F 0x38
// runtime/simd.h:178
#define SF2C_SIMD_N_0XF5_3D72 0xf5
// runtime/simd.h:178
#define SF2C_SIMD_N_0XBC_77C1 0xbc
// runtime/simd.h:178
#define SF2C_SIMD_N_0XB6_99C6 0xb6
// runtime/simd.h:178
#define SF2C_SIMD_N_0XDA_9ACA 0xda
// runtime/simd.h:178
#define SF2C_SIMD_N_0X21_A102 0x21
// runtime/simd.h:178
#define SF2C_SIMD_N_0X10_BF9C 0x10
// runtime/simd.h:178
#define SF2C_SIMD_N_0XFF_0273 0xff
// runtime/simd.h:178
#define SF2C_SIMD_N_0XF3_069A 0xf3
// runtime/simd.h:178
#define SF2C_SIMD_N_0XD2_C8EC 0xd2
// runtime/simd.h:179
#define SF2C_SIMD_N_0XCD_AA46 0xcd
// runtime/simd.h:179
#define SF2C_SIMD_N_0X0C_B6C2 0x0c
// runtime/simd.h:179
#define SF2C_SIMD_N_0X13_3ABB 0x13
// runtime/simd.h:179
#define SF2C_SIMD_N_0XEC_6B4E 0xec
// runtime/simd.h:179
#define SF2C_SIMD_N_0X5F_E7A1 0x5f
// runtime/simd.h:179
#define SF2C_SIMD_N_0X97_6139 0x97
// runtime/simd.h:179
#define SF2C_SIMD_N_0X44_E723 0x44
// runtime/simd.h:179
#define SF2C_SIMD_N_0X17_A99E 0x17
// runtime/simd.h:179
#define SF2C_SIMD_N_0XC4_EDF1 0xc4
// runtime/simd.h:179
#define SF2C_SIMD_N_0XA7_BB20 0xa7
// runtime/simd.h:179
#define SF2C_SIMD_N_0X7E_BFE8 0x7e
// runtime/simd.h:179
#define SF2C_SIMD_N_0X3D_15F4 0x3d
// runtime/simd.h:179
#define SF2C_SIMD_N_0X64_3246 0x64
// runtime/simd.h:179
#define SF2C_SIMD_N_0X5D_A8C5 0x5d
// runtime/simd.h:179
#define SF2C_SIMD_N_0X19_6E00 0x19
// runtime/simd.h:179
#define SF2C_SIMD_N_0X73_9408 0x73
// runtime/simd.h:180
#define SF2C_SIMD_N_0X60_3D6E 0x60
// runtime/simd.h:180
#define SF2C_SIMD_N_0X81_C29D 0x81
// runtime/simd.h:180
#define SF2C_SIMD_N_0X4F_66E2 0x4f
// runtime/simd.h:180
#define SF2C_SIMD_N_0XDC_2690 0xdc
// runtime/simd.h:180
#define SF2C_SIMD_N_0X22_9F8A 0x22
// runtime/simd.h:180
#define SF2C_SIMD_N_0X2A_FD67 0x2a
// runtime/simd.h:180
#define SF2C_SIMD_N_0X90_4255 0x90
// runtime/simd.h:180
#define SF2C_SIMD_N_0X88_AC70 0x88
// runtime/simd.h:180
#define SF2C_SIMD_N_0X46_E7C6 0x46
// runtime/simd.h:180
#define SF2C_SIMD_N_0XEE_994B 0xee
// runtime/simd.h:180
#define SF2C_SIMD_N_0XB8_8F73 0xb8
// runtime/simd.h:180
#define SF2C_SIMD_N_0X14_F942 0x14
// runtime/simd.h:180
#define SF2C_SIMD_N_0XDE_1D39 0xde
// runtime/simd.h:180
#define SF2C_SIMD_N_0X5E_521D 0x5e
// runtime/simd.h:180
#define SF2C_SIMD_N_0X0B_7C21 0x0b
// runtime/simd.h:180
#define SF2C_SIMD_N_0XDB_9D44 0xdb
// runtime/simd.h:181
#define SF2C_SIMD_N_0XE0_F5C1 0xe0
// runtime/simd.h:181
#define SF2C_SIMD_N_0X32_8B2B 0x32
// runtime/simd.h:181
#define SF2C_SIMD_N_0X3A_490F 0x3a
// runtime/simd.h:181
#define SF2C_SIMD_N_0X0A_CE08 0x0a
// runtime/simd.h:181
#define SF2C_SIMD_N_0X49_823D 0x49
// runtime/simd.h:181
#define SF2C_SIMD_N_0X06_9AE3 0x06
// runtime/simd.h:181
#define SF2C_SIMD_N_0X24_EB21 0x24
// runtime/simd.h:181
#define SF2C_SIMD_N_0X5C_F9CC 0x5c
// runtime/simd.h:181
#define SF2C_SIMD_N_0XC2_0982 0xc2
// runtime/simd.h:181
#define SF2C_SIMD_N_0XD3_CEBE 0xd3
// runtime/simd.h:181
#define SF2C_SIMD_N_0XAC_3360 0xac
// runtime/simd.h:181
#define SF2C_SIMD_N_0X62_88BC 0x62
// runtime/simd.h:181
#define SF2C_SIMD_N_0X91_BC34 0x91
// runtime/simd.h:181
#define SF2C_SIMD_N_0X95_ADEA 0x95
// runtime/simd.h:181
#define SF2C_SIMD_N_0XE4_D5C0 0xe4
// runtime/simd.h:181
#define SF2C_SIMD_N_0X79_A3F8 0x79
// runtime/simd.h:182
#define SF2C_SIMD_N_0XE7_0592 0xe7
// runtime/simd.h:182
#define SF2C_SIMD_N_0XC8_62CB 0xc8
// runtime/simd.h:182
#define SF2C_SIMD_N_0X37_031A 0x37
// runtime/simd.h:182
#define SF2C_SIMD_N_0X6D_CAE4 0x6d
// runtime/simd.h:182
#define SF2C_SIMD_N_0X8D_9078 0x8d
// runtime/simd.h:182
#define SF2C_SIMD_N_0XD5_52D3 0xd5
// runtime/simd.h:182
#define SF2C_SIMD_N_0X4E_2C8D 0x4e
// runtime/simd.h:182
#define SF2C_SIMD_N_0XA9_C008 0xa9
// runtime/simd.h:182
#define SF2C_SIMD_N_0X6C_3CD4 0x6c
// runtime/simd.h:182
#define SF2C_SIMD_N_0X56_1DDC 0x56
// runtime/simd.h:182
#define SF2C_SIMD_N_0XF4_A61B 0xf4
// runtime/simd.h:182
#define SF2C_SIMD_N_0XEA_6E8E 0xea
// runtime/simd.h:182
#define SF2C_SIMD_N_0X65_0237 0x65
// runtime/simd.h:182
#define SF2C_SIMD_N_0X7A_4A3E 0x7a
// runtime/simd.h:182
#define SF2C_SIMD_N_0XAE_5BBA 0xae
// runtime/simd.h:182
#define SF2C_SIMD_N_0X08_1EC8 0x08
// runtime/simd.h:183
#define SF2C_SIMD_N_0XBA_C3A8 0xba
// runtime/simd.h:183
#define SF2C_SIMD_N_0X78_756D 0x78
// runtime/simd.h:183
#define SF2C_SIMD_N_0X25_2611 0x25
// runtime/simd.h:183
#define SF2C_SIMD_N_0X2E_9165 0x2e
// runtime/simd.h:183
#define SF2C_SIMD_N_0X1C_7BDE 0x1c
// runtime/simd.h:183
#define SF2C_SIMD_N_0XA6_FDD7 0xa6
// runtime/simd.h:183
#define SF2C_SIMD_N_0XB4_F215 0xb4
// runtime/simd.h:183
#define SF2C_SIMD_N_0XC6_BD59 0xc6
// runtime/simd.h:183
#define SF2C_SIMD_N_0XE8_7494 0xe8
// runtime/simd.h:183
#define SF2C_SIMD_N_0XDD_F2F8 0xdd
// runtime/simd.h:183
#define SF2C_SIMD_N_0X74_D298 0x74
// runtime/simd.h:183
#define SF2C_SIMD_N_0X1F_9227 0x1f
// runtime/simd.h:183
#define SF2C_SIMD_N_0X4B_969D 0x4b
// runtime/simd.h:183
#define SF2C_SIMD_N_0XBD_1B5C 0xbd
// runtime/simd.h:183
#define SF2C_SIMD_N_0X8B_834C 0x8b
// runtime/simd.h:183
#define SF2C_SIMD_N_0X8A_5837 0x8a
// runtime/simd.h:184
#define SF2C_SIMD_N_0X70_B0F6 0x70
// runtime/simd.h:184
#define SF2C_SIMD_N_0X3E_B121 0x3e
// runtime/simd.h:184
#define SF2C_SIMD_N_0XB5_D55E 0xb5
// runtime/simd.h:184
#define SF2C_SIMD_N_0X66_409E 0x66
// runtime/simd.h:184
#define SF2C_SIMD_N_0X48_0EF0 0x48
// runtime/simd.h:184
#define SF2C_SIMD_N_0X03_9D1B 0x03
// runtime/simd.h:184
#define SF2C_SIMD_N_0XF6_8734 0xf6
// runtime/simd.h:184
#define SF2C_SIMD_N_0X0E_80FB 0x0e
// runtime/simd.h:184
#define SF2C_SIMD_N_0X61_DB35 0x61
// runtime/simd.h:184
#define SF2C_SIMD_N_0X35_B555 0x35
// runtime/simd.h:184
#define SF2C_SIMD_N_0X57_F1A3 0x57
// runtime/simd.h:184
#define SF2C_SIMD_N_0XB9_24A3 0xb9
// runtime/simd.h:184
#define SF2C_SIMD_N_0X86_0DF0 0x86
// runtime/simd.h:184
#define SF2C_SIMD_N_0XC1_67A9 0xc1
// runtime/simd.h:184
#define SF2C_SIMD_N_0X1D_175D 0x1d
// runtime/simd.h:184
#define SF2C_SIMD_N_0X9E_6631 0x9e
// runtime/simd.h:185
#define SF2C_SIMD_N_0XE1_0BF1 0xe1
// runtime/simd.h:185
#define SF2C_SIMD_N_0XF8_8EBA 0xf8
// runtime/simd.h:185
#define SF2C_SIMD_N_0X98_14A1 0x98
// runtime/simd.h:185
#define SF2C_SIMD_N_0X11_A3D6 0x11
// runtime/simd.h:185
#define SF2C_SIMD_N_0X69_3172 0x69
// runtime/simd.h:185
#define SF2C_SIMD_N_0XD9_3960 0xd9
// runtime/simd.h:185
#define SF2C_SIMD_N_0X8E_CF35 0x8e
// runtime/simd.h:185
#define SF2C_SIMD_N_0X94_EB0B 0x94
// runtime/simd.h:185
#define SF2C_SIMD_N_0X9B_5C1B 0x9b
// runtime/simd.h:185
#define SF2C_SIMD_N_0X1E_1CBB 0x1e
// runtime/simd.h:185
#define SF2C_SIMD_N_0X87_1A28 0x87
// runtime/simd.h:185
#define SF2C_SIMD_N_0XE9_F94F 0xe9
// runtime/simd.h:185
#define SF2C_SIMD_N_0XCE_832E 0xce
// runtime/simd.h:185
#define SF2C_SIMD_N_0X55_70B3 0x55
// runtime/simd.h:185
#define SF2C_SIMD_N_0X28_EAB9 0x28
// runtime/simd.h:185
#define SF2C_SIMD_N_0XDF_F3B9 0xdf
// runtime/simd.h:186
#define SF2C_SIMD_N_0X8C_65B3 0x8c
// runtime/simd.h:186
#define SF2C_SIMD_N_0XA1_F57A 0xa1
// runtime/simd.h:186
#define SF2C_SIMD_N_0X89_72E9 0x89
// runtime/simd.h:186
#define SF2C_SIMD_N_0X0D_DDF9 0x0d
// runtime/simd.h:186
#define SF2C_SIMD_N_0XBF_7F9B 0xbf
// runtime/simd.h:186
#define SF2C_SIMD_N_0XE6_C0CB 0xe6
// runtime/simd.h:186
#define SF2C_SIMD_N_0X42_1114 0x42
// runtime/simd.h:186
#define SF2C_SIMD_N_0X68_40D3 0x68
// runtime/simd.h:186
#define SF2C_SIMD_N_0X41_BE08 0x41
// runtime/simd.h:186
#define SF2C_SIMD_N_0X99_093C 0x99
// runtime/simd.h:186
#define SF2C_SIMD_N_0X2D_BAF3 0x2d
// runtime/simd.h:186
#define SF2C_SIMD_N_0X0F_33C5 0x0f
// runtime/simd.h:186
#define SF2C_SIMD_N_0XB0_D785 0xb0
// runtime/simd.h:186
#define SF2C_SIMD_N_0X54_9433 0x54
// runtime/simd.h:186
#define SF2C_SIMD_N_0XBB_F392 0xbb
// runtime/simd.h:186
#define SF2C_SIMD_N_0X16_D7D4 0x16
// runtime/simd.h:226
#define SF2C_SIMD_N_0X6000U_9D61 0x6000u
// runtime/simd.h:280
#define SF2C_SIMD_N_0X7FF8000000000000ULL_24D6 0x7ff8000000000000ull
// runtime/simd.h:281
#define SF2C_SIMD_N_0X3FFFFF_01A0 0x3fffff
// runtime/simd.h:297
#define SF2C_SIMD_N_0X7FC00000U_E20D 0x7fc00000u
// runtime/simd.h:302
#define SF2C_SIMD_N_0X1P_126_5F0C 0x1p-126
// runtime/simd.h:323
#define SF2C_SIMD_N_1ULL_622E 1ull
// runtime/simd.h:334
#define SF2C_SIMD_N_0X7FF_43AE 0x7ff
// runtime/simd.h:337
#define SF2C_SIMD_N_0X7E00_C39E 0x7e00
// runtime/simd.h:338
#define SF2C_SIMD_N_0X7FFF_D189 0x7fff
// runtime/simd.h:338
#define SF2C_SIMD_N_0X7C00_B0FD 0x7c00
// runtime/simd.h:342
#define SF2C_SIMD_N_1075_337B 1075
// runtime/simd.h:342
#define SF2C_SIMD_N_1074_FDE3 1074
// runtime/simd.h:342
#define SF2C_SIMD_N_1023_6629 1023
// runtime/simd.h:355
#define SF2C_SIMD_N_0X7BFF_86A2 0x7bff
// runtime/simd.h:361
#define SF2C_SIMD_N_0X7FF0000000000000ULL_9CC3 0x7ff0000000000000ull
// runtime/simd.h:361
#define SF2C_SIMD_N_0X7FFFFF_3AB1 0x7fffff
// runtime/simd.h:368
#define SF2C_SIMD_N_0X3FF_6900 0x3ff
// runtime/simd.h:374
#define SF2C_SIMD_N_0X1P_24_BF6A 0x1p-24
// runtime/simd.h:404
#define SF2C_SIMD_N_0ULL_DF1B 0ull
// runtime/simd.h:516
#define SF2C_SIMD_N_0XFFFFFFFFULL_4EE8 0xffffffffull
// runtime/simd.h:517
#define SF2C_SIMD_N_511_2C69 511
// runtime/simd.h:605
#define SF2C_SIMD_N_0X80000000U_AE05 0x80000000u
// runtime/simd.h:606
#define SF2C_SIMD_N_0X8000000000000000ULL_1BED 0x8000000000000000ull
// runtime/simd.h:629
#define SF2C_SIMD_N_2_0F_2E19 2.0f
// runtime/simd.h:679
#define SF2C_SIMD_N_16U_1FB3 16u
// runtime/simd_fp.h:63
#define SF2C_SIMD_FP_N_0_5_D2CB 0.5
// runtime/simd_fp.h:71
#define SF2C_SIMD_FP_N_1_5_9F29 1.5
// runtime/simd_fp.h:195
#define SF2C_SIMD_FP_N_0X60000000ULL_AC90 0x60000000ull
// runtime/simd_fp.h:195
#define SF2C_SIMD_FP_N_0X80000000ULL_6840 0x80000000ull
// runtime/simd_fp.h:195
#define SF2C_SIMD_FP_N_0X20000000ULL_ADEC 0x20000000ull
// runtime/simd_fp.h:195
#define SF2C_SIMD_FP_N_0X30000000ULL_CD09 0x30000000ull
// runtime/simd_fp.h:221
#define SF2C_SIMD_FP_N_22_785F 22
// runtime/simd_fp.h:246
#define SF2C_SIMD_FP_N_1ULL_622E 1ull
// runtime/simd_fp.h:251
#define SF2C_SIMD_FP_N_32_E29C 32
// runtime/simd_fp.h:251
#define SF2C_SIMD_FP_N_4294967296_0_6B53 4294967296.0
// runtime/simd_fp.h:251
#define SF2C_SIMD_FP_N_0XFFFFFFFFULL_4EE8 0xffffffffull
// runtime/simd_fp.h:252
#define SF2C_SIMD_FP_N_18446744073709551616_0_CCF7 18446744073709551616.0
// runtime/simd_fp.h:252
#define SF2C_SIMD_FP_N_0ULL_DF1B 0ull
// runtime/simd_fp.h:254
#define SF2C_SIMD_FP_N_2147483648_0_1E55 2147483648.0
// runtime/simd_fp.h:254
#define SF2C_SIMD_FP_N_0X7FFFFFFFULL_BD13 0x7fffffffull
// runtime/simd_fp.h:255
#define SF2C_SIMD_FP_N_9223372036854775808_0_19A5 9223372036854775808.0
// runtime/simd_fp.h:255
#define SF2C_SIMD_FP_N_0X7FFFFFFFFFFFFFFFULL_1E66 0x7fffffffffffffffull
// runtime/simd_fp.h:255
#define SF2C_SIMD_FP_N_0X8000000000000000ULL_1BED 0x8000000000000000ull
// runtime/simd_fp.h:301
#define SF2C_SIMD_FP_N_256_51E8 256
// runtime/simd_fp.h:321
#define SF2C_SIMD_FP_N_128_2747 128

#define SF2C_DISPATCH_MAX_HLE 4096

#define SF2C_DISPATCH_DCACHE 4096

#define SF2C_GAME_INFO_SF2_APP_NAME "Shadow Fight 2 (sf2recomp)"

#define SF2C_GAME_INFO_SF2_APP_ID "org.sf2recomp.ShadowFight2"

#define SF2C_HLE_HLE_SECTION "__DATA,__hle_impls"


#define SF2C_HLE_RES_OK 0u

#define SF2C_INPUT_BIND_BIND_CAPTURE_MS 5000


#define SF2C_KERNEL_R_TIMEOUT 0xEA01

#define SF2C_KERNEL_R_CANCELLED 0xEC01

#define SF2C_KERNEL_R_INVALID_HANDLE 0xE401

#define SF2C_KERNEL_R_INVALID_STATE 0xFA01

#define SF2C_KERNEL_R_INVALID_ADDRESS 0xCC01

#define SF2C_KERNEL_R_NOT_FOUND 0xF201

#define SF2C_KERNEL_R_INVALID_ENUM 0xF001

#define SF2C_KERNEL_R_OUT_OF_RESOURCE 0xD001

#define SF2C_KERNEL_PSEUDO_THREAD 0xFFFF8000u

#define SF2C_KERNEL_PSEUDO_PROCESS 0xFFFF8001u

#define SF2C_KERNEL_WAITERS_BIT 0x40000000u

#define SF2C_KERNEL_MAX_HANDLES 4096

#define SF2C_KERNEL_KTRACE_N 256

#define SF2C_KERNEL_STACK_BASE 0x7200000000ull

#define SF2C_KERNEL_STACK_SIZE 0x0040000000ull

#define SF2C_MAIN_REEXEC_FORCED "forced by LARECOMP_REEXEC_FORCE"


#define SF2C_PLATFORM_PLAT_VENV_PYTHON "venv/bin/python"

#define SF2C_ROMFS_BUILD_EMPTY 0xFFFFFFFFu

#define SF2C_ROMFS_BUILD_ROMFS_DATA_START 0x200u

#define SF2C_RT_HLE_HEAP_SIZE 0x000E000000ull

#define SF2C_SETTINGS_SETTINGS_SAVE_DIRECTORY_CAP 768



// runtime/cpu.h:44
#define SF2C_CPU_N_3_4E07 3
// runtime/cpu.h:223
#define SF2C_CPU_N_8_2C62 8
// runtime/dispatch.c:16
#define SF2C_DISPATCH_N_2_D473 2
// runtime/dispatch.c:227
#define SF2C_DISPATCH_N_4_4B22 4
// runtime/dispatch.c:228
#define SF2C_DISPATCH_N_8_2C62 8
// runtime/dispatch.c:291
#define SF2C_DISPATCH_N_3_4E07 3
// runtime/exception.c:32
#define SF2C_EXCEPTION_N_4_4B22 4
// runtime/fnprof.c:39
#define SF2C_FNPROF_N_8_2C62 8
// runtime/fnprof.c:49
#define SF2C_FNPROF_N_2_D473 2
// runtime/game_addresses.c:26
#define SF2C_GAME_ADDRESSES_N_2_D473 2
// runtime/game_addresses.c:26
#define SF2C_GAME_ADDRESSES_N_4_4B22 4
// runtime/game_addresses.c:67
#define SF2C_GAME_ADDRESSES_N_3_4E07 3
// runtime/game_addresses.c:67
#define SF2C_GAME_ADDRESSES_N_5_EF2D 5
// runtime/game_addresses.c:67
#define SF2C_GAME_ADDRESSES_N_6_E7F6 6
// runtime/game_addresses.c:67
#define SF2C_GAME_ADDRESSES_N_7_7902 7
// runtime/game_info.c:34
#define SF2C_GAME_INFO_N_2_D473 2
// runtime/game_info.c:49
#define SF2C_GAME_INFO_N_8_2C62 8
// runtime/game_info.c:68
#define SF2C_GAME_INFO_N_3_4E07 3
// runtime/gamelib.c:19
#define SF2C_GAMELIB_N_2_D473 2
// runtime/gamelib.c:27
#define SF2C_GAMELIB_N_4_4B22 4
// runtime/hle.h:48
#define SF2C_HLE_N_9_1958 9
// runtime/ipc.c:77
#define SF2C_IPC_N_9_1958 9
// runtime/ipc.c:78
#define SF2C_IPC_N_8_2C62 8
// runtime/ipc.c:90
#define SF2C_IPC_N_2_D473 2
// runtime/ipc.c:90
#define SF2C_IPC_N_7_7902 7
// runtime/ipc.c:112
#define SF2C_IPC_N_5_EF2D 5
// runtime/ipc.c:117
#define SF2C_IPC_N_6_E7F6 6
// runtime/ipc.c:121
#define SF2C_IPC_N_3_4E07 3
// runtime/ipc.c:151
#define SF2C_IPC_N_4_4B22 4
// runtime/kernel.c:124
#define SF2C_KERNEL_N_2_D473 2
// runtime/kernel.c:150
#define SF2C_KERNEL_N_5_EF2D 5
// runtime/kernel.c:157
#define SF2C_KERNEL_N_3_4E07 3
// runtime/kernel.c:215
#define SF2C_KERNEL_N_8_2C62 8
// runtime/kernel.c:672
#define SF2C_KERNEL_N_4_4B22 4
// runtime/kernel.c:787
#define SF2C_KERNEL_N_6_E7F6 6
// runtime/kernel.c:867
#define SF2C_KERNEL_N_7_7902 7
// runtime/kernel.c:869
#define SF2C_KERNEL_N_9_1958 9
// runtime/lan.c:41
#define SF2C_LAN_N_2_D473 2
// runtime/lan.c:58
#define SF2C_LAN_N_8_2C62 8
// runtime/lan.c:108
#define SF2C_LAN_N_3_4E07 3
// runtime/lan.c:153
#define SF2C_LAN_N_4_4B22 4
// runtime/lan.h:8
#define SF2C_LAN_N_6_E7F6 6
// runtime/loader.c:8
#define SF2C_LOADER_N_4_4B22 4
// runtime/loader.c:11
#define SF2C_LOADER_N_2_D473 2
// runtime/loader.c:11
#define SF2C_LOADER_N_5_EF2D 5
// runtime/loader.c:11
#define SF2C_LOADER_N_6_E7F6 6
// runtime/loader.c:11
#define SF2C_LOADER_N_7_7902 7
// runtime/loader.c:11
#define SF2C_LOADER_N_8_2C62 8
// runtime/loader.c:58
#define SF2C_LOADER_N_3_4E07 3
// runtime/main.c:25
#define SF2C_MAIN_N_8_2C62 8
// runtime/main.c:89
#define SF2C_MAIN_N_4_4B22 4
// runtime/main.c:122
#define SF2C_MAIN_N_2_D473 2
// runtime/network.c:44
#define SF2C_NETWORK_N_5_EF2D 5
// runtime/network.c:67
#define SF2C_NETWORK_N_3_4E07 3
// runtime/network.c:76
#define SF2C_NETWORK_N_4_4B22 4
// runtime/network.c:81
#define SF2C_NETWORK_N_2_D473 2
// runtime/paths.c:22
#define SF2C_PATHS_N_2_D473 2
// runtime/platform.h:38
#define SF2C_PLATFORM_N_3_4E07 3
// runtime/replay.c:30
#define SF2C_REPLAY_N_2_D473 2
// runtime/replay.c:32
#define SF2C_REPLAY_N_3_4E07 3
// runtime/replay.c:112
#define SF2C_REPLAY_N_5_EF2D 5
// runtime/replay.c:115
#define SF2C_REPLAY_N_4_4B22 4
// runtime/replay.c:284
#define SF2C_REPLAY_N_8_2C62 8
// runtime/replay.c:331
#define SF2C_REPLAY_N_7_7902 7
// runtime/replay.c:389
#define SF2C_REPLAY_N_6_E7F6 6
// runtime/romfs_build.c:54
#define SF2C_ROMFS_BUILD_N_2_D473 2
// runtime/romfs_build.c:63
#define SF2C_ROMFS_BUILD_N_8_2C62 8
// runtime/romfs_build.c:104
#define SF2C_ROMFS_BUILD_N_5_EF2D 5
// runtime/romfs_build.c:108
#define SF2C_ROMFS_BUILD_N_3_4E07 3
// runtime/romfs_build.c:111
#define SF2C_ROMFS_BUILD_N_7_7902 7
// runtime/romfs_build.c:117
#define SF2C_ROMFS_BUILD_N_4_4B22 4
// runtime/rt.h:14
#define SF2C_RT_N_2_D473 2
// runtime/rt.h:56
#define SF2C_RT_N_4_4B22 4
// runtime/rumble.h:17
#define SF2C_RUMBLE_N_2_D473 2
// runtime/rumble.h:17
#define SF2C_RUMBLE_N_4_4B22 4
// runtime/rumble.h:24
#define SF2C_RUMBLE_N_8_2C62 8
// runtime/rumble.h:26
#define SF2C_RUMBLE_N_3_4E07 3
// runtime/rumble.h:26
#define SF2C_RUMBLE_N_5_EF2D 5
// runtime/settings.c:249
#define SF2C_SETTINGS_N_2_D473 2
// runtime/settings.c:250
#define SF2C_SETTINGS_N_3_4E07 3
// runtime/settings.c:251
#define SF2C_SETTINGS_N_4_4B22 4
// runtime/settings.h:11
#define SF2C_SETTINGS_N_5_EF2D 5
// runtime/settings.h:12
#define SF2C_SETTINGS_N_9_1958 9
// runtime/simd.h:74
#define SF2C_SIMD_N_9_1958 9
// runtime/simd.h:75
#define SF2C_SIMD_N_8_2C62 8
// runtime/simd.h:75
#define SF2C_SIMD_N_7_7902 7
// runtime/simd.h:75
#define SF2C_SIMD_N_6_E7F6 6
// runtime/simd.h:75
#define SF2C_SIMD_N_5_EF2D 5
// runtime/simd.h:75
#define SF2C_SIMD_N_4_4B22 4
// runtime/simd.h:75
#define SF2C_SIMD_N_3_4E07 3
// runtime/simd.h:75
#define SF2C_SIMD_N_2_D473 2
// runtime/simd_fp.h:63
#define SF2C_SIMD_FP_N_2_D473 2
// runtime/simd_fp.h:64
#define SF2C_SIMD_FP_N_3_4E07 3
// runtime/simd_fp.h:222
#define SF2C_SIMD_FP_N_5_EF2D 5
// runtime/simd_fp.h:302
#define SF2C_SIMD_FP_N_8_2C62 8
// runtime/simd_fp.h:322
#define SF2C_SIMD_FP_N_7_7902 7


#define SF2C_IPC_SERVICE_SECTION_521B1F95 "services"

#define SF2C_IPC_SERVICE_SECTION_FB6AB980 "__DATA,__services"

#define SF2C_HLE_RESET_SECTION_C31C6D41 "resets"

#define SF2C_HLE_RESET_SECTION_8FA56F34 "__DATA,__rt_reset"

#define SF2C_SIMD_EB_4FC82B26 11

#define SF2C_SIMD_EB_2C624232 8

#define SF2C_SIMD_NL_D4735E3A 2

#define SF2C_SIMD_NL_4B227777 4

#define SF2C_SIMD_NB_A68B412C 64

#define SF2C_SIMD_NB_E29C9C18 32

#define SF2C_SIMD_MB_41CFC0D1 52

#define SF2C_SIMD_MB_535FA30D 23

#define SF2C_PLATFORM_PLAT_EXE_SUFFIX_12AE32CB ""

#define SF2C_PLATFORM_PLAT_EXE_SUFFIX_8418E474 ".exe"
// runtime/main.c:148
#define SF2C_MAIN_S_SF2_GAME_LOG_74EB "SF2_GAME_LOG"
// runtime/main.c:148
#define SF2C_MAIN_S_1_3915 "1"
