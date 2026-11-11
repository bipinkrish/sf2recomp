# SF2 Recomp build (the runtime is LAS Recomp's MIT core; see licenses/LASRecomp.txt)
CC  := clang
CXX := clang++
.DEFAULT_GOAL := all
ifeq ($(OS),Windows_NT)
TARGET  ?= windows
else ifeq ($(shell uname -s),Darwin)
TARGET  ?= macos
else
TARGET  ?= linux
endif
ifeq ($(filter $(TARGET),linux windows macos),)
$(error Unsupported TARGET=$(TARGET); use linux, windows or macos)
endif
B       ?= $(if $(filter linux,$(TARGET)),build,build/$(TARGET))
GEN     ?= game/gen
GAME    ?= game/extracted/update
PYTHON  ?= python3
SIMD    ?= $(if $(filter arm64 aarch64,$(shell uname -m)),asm,c)
SDL_DIR ?= $(if $(filter windows,$(TARGET)),$(or $(MINGW_PREFIX),$(CURDIR)/deps/windows),$(if $(filter macos,$(TARGET)),$(CURDIR)/deps/macos,$(CURDIR)/deps))
# Default to the local prefix that runtime-deps can prepare. Prefer a suitable
# system SDL only when there is no complete local installation.
SDL_CFLAGS := -I$(SDL_DIR)/include
SDL_LIBS := -L$(SDL_DIR)/lib -lSDL3 -Wl,-rpath,$(SDL_DIR)/lib
ifeq ($(TARGET),windows)
SDL_CFLAGS := -I$(SDL_DIR)/include
SDL_LIBS := -L$(SDL_DIR)/lib -lSDL3
endif
ifeq ($(and $(wildcard $(SDL_DIR)/include/SDL3/SDL.h),$(wildcard $(SDL_DIR)/lib/libSDL3.*)),)
ifeq ($(shell pkg-config --atleast-version=3.4.0 sdl3 2>/dev/null && echo yes),yes)
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
endif
endif

# The generated game code (tools/recompr/gen.py), compiled like the generator expects:
GENFLAGS := -O1 -mcx16 -fno-strict-aliasing -Wno-return-type -Wno-unused-label -Wno-unused-command-line-argument -fvisibility=hidden -fPIC -Iruntime
RTFLAGS  := -O2 -g -mcx16 -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format \
            -Iruntime -Iruntime/nvn -Ithird_party/smaa -Ithird_party/smaa/Textures $(SDL_CFLAGS) -MMD -MP
CXXFLAGS := -O2 -g -mcx16 -std=c++17 -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format \
            -Iruntime -Iruntime/nvn -Ithird_party/smaa -Ithird_party/smaa/Textures $(SDL_CFLAGS) -MMD -MP

SHADERC  ?= $(shell pkg-config --libs shaderc 2>/dev/null || echo -lshaderc)
LDFLAGS  := -fuse-ld=lld -rdynamic -Wl,-rpath,'$$ORIGIN/lib' $(SDL_LIBS) -lvulkan $(SHADERC) -latomic -pthread -lm -ldl

EXE := $(B)/sf2
ifeq ($(TARGET),macos)
# Apple Silicon has native 128-bit atomics; -mcx16 is an x86-only switch.
HOST_ARCH := $(shell uname -m)
MACOS_MIN ?= 13.0
MACFLAGS := -mmacosx-version-min=$(MACOS_MIN) $(if $(filter x86_64,$(HOST_ARCH)),-mcx16)
GENFLAGS := $(filter-out -mcx16,$(GENFLAGS)) $(MACFLAGS)
RTFLAGS := $(filter-out -mcx16,$(RTFLAGS)) $(MACFLAGS) -Ideps/vma/include $(shell pkg-config --cflags vulkan shaderc)
CXXFLAGS := $(filter-out -mcx16,$(CXXFLAGS)) $(MACFLAGS) -Ideps/vma/include $(shell pkg-config --cflags vulkan shaderc)
# Darwin's system linker/libSystem supply atomics, pthread, math and dl APIs.
LDFLAGS := $(MACFLAGS) -Wl,-headerpad_max_install_names -Wl,-rpath,@executable_path/lib $(SDL_LIBS) \
           $(shell pkg-config --libs vulkan shaderc) -framework CoreFoundation
endif
ifeq ($(TARGET),windows)
OSFLAGS := -D_FILE_OFFSET_BITS=64 -D_POSIX_THREAD_SAFE_FUNCTIONS -D_CRT_RAND_S \
           -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00
GENFLAGS := $(filter-out -fPIC,$(GENFLAGS)) $(OSFLAGS)
RTFLAGS += $(OSFLAGS)
CXXFLAGS += $(OSFLAGS)
SHADERC := $(shell pkg-config --libs shaderc 2>/dev/null || echo -lshaderc_shared)
LDFLAGS := -fuse-ld=lld -mwindows -Wl,--stack,16777216 $(SDL_LIBS) -lvulkan-1 $(SHADERC) \
           -pthread -lm -lws2_32 -lwinmm -lshell32 -lole32 -ladvapi32 -luser32
EXE := $(B)/sf2.exe
WINDRES ?= windres
else
POSIX_LAUNCHER_FLAGS := $(if $(filter macos,$(TARGET)),$(MACFLAGS),-no-pie)
$(B)/sf2recomp: tools/posix_launcher.c Makefile
	@mkdir -p $(dir $@)
	$(CC) -O2 -Wall $(POSIX_LAUNCHER_FLAGS) $< -o $@

.PHONY: desktop-launcher
desktop-launcher: $(B)/sf2recomp
endif

# Native host gate, independent of proprietary files and graphics initialization.
.PHONY: macos-smoke
macos-smoke:
	@test "$(TARGET)" = macos || { echo "Use make TARGET=macos macos-smoke on macOS" >&2; exit 1; }
	@mkdir -p $(B)
	$(CC) $(MACFLAGS) -Wall -Iruntime tests/macos_platform.c runtime/platform/macos.c runtime/paths.c \
	    -o $(B)/macos-platform-test -framework CoreFoundation
	$(B)/macos-platform-test

# Download pinned SMAA sources only when absent; generated files stay out of Git.
SMAA_INPUTS := third_party/smaa/smaa_hlsl.inc third_party/smaa/Textures/AreaTex.h third_party/smaa/Textures/SearchTex.h
$(SMAA_INPUTS) &: tools/setup_smaa.py | runtime-deps
	$(PYTHON) tools/setup_smaa.py
	@touch $(SMAA_INPUTS)

$(B)/rt/nvn/smaa_data.o: $(SMAA_INPUTS)

GEN_SRC := $(wildcard $(GEN)/main/*.c) $(wildcard $(GEN)/sdk/*.c)
RT_SRC  := $(wildcard runtime/*.c) $(wildcard runtime/hle/*.c) $(wildcard runtime/services/*.c) \
           $(wildcard runtime/nvn/*.c) $(wildcard runtime/nvn/maxwell/*.c) $(wildcard runtime/nvn/vulkan/*.c) \
           $(wildcard runtime/ui/*.c) runtime/platform/$(TARGET).c
RT_CXX_SRC := $(wildcard runtime/nvn/vulkan/*.cpp)

GEN_OBJ := $(patsubst $(GEN)/%.c,$(B)/obj/%.o,$(GEN_SRC))
RT_OBJ  := $(patsubst runtime/%.c,$(B)/rt/%.o,$(RT_SRC)) $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(RT_CXX_SRC))
ifeq ($(TARGET),windows)
RT_OBJ += $(B)/rt/platform/windows_rc.o
WINDOWS_RESOURCES := runtime/platform/windows.rc runtime/platform/windows.manifest assets/sf2recomp.ico runtime/version.h runtime/lan_protocol.h tools/version.py
WINDOWS_RC_FLAGS = -Iruntime/platform -Iassets -DSF2_WINDOWS_VERSION=$(shell $(PYTHON) tools/version.py --windows) -O coff
$(B)/rt/platform/windows_rc.o: $(WINDOWS_RESOURCES) Makefile
	@mkdir -p $(dir $@)
	$(WINDRES) $(WINDOWS_RC_FLAGS) $< -o $@

$(B)/converter_rc.o: $(WINDOWS_RESOURCES) Makefile
	@mkdir -p $(dir $@)
	$(WINDRES) $(WINDOWS_RC_FLAGS) -DSF2_CONVERTER $< -o $@

$(B)/sf2recomp.exe: tools/windows_launcher.c $(B)/converter_rc.o Makefile
	$(CC) -O2 -Wall -municode -mwindows -fuse-ld=lld $(filter-out Makefile,$^) -lshell32 -luser32 -o $@

.PHONY: windows-launcher
windows-launcher: $(B)/sf2recomp.exe
endif
DEPS    := $(RT_OBJ:.o=.d)

all: $(EXE) $(B)/sf2recomp.ini

.PHONY: all gen check-generated runtime-objs clean
gen:
	@test -f "$(GAME)/exefs/main" -a -f "$(GAME)/exefs/sdk" || { echo "Extract the supported game first: tools/extract.sh NSP KEYS" >&2; exit 1; }
	@test -f game/il2cpp/sym_main.txt || { echo "Generate the symbol map first: tools/il2cpp_run.sh $(GAME)" >&2; exit 1; }
	$(PYTHON) tools/recompr/gen.py main "$(GAME)/exefs/main" 0x7000000000 "$(GEN)/main" --symbols game/il2cpp/sym_main.txt --simd=$(SIMD)
	$(PYTHON) tools/recompr/gen.py sdk "$(GAME)/exefs/sdk" 0x7004000000 "$(GEN)/sdk" --simd=$(SIMD)

check-generated:
	@grep -q main_nso_build_id "$(GEN)/main/main_table.c" 2>/dev/null && grep -q sdk_nso_build_id "$(GEN)/sdk/sdk_table.c" 2>/dev/null || { echo "Generated code is missing/outdated. Run make gen or tools/convert.py first." >&2; exit 1; }

# Preserve runtime edits on subsequent builds. The file is never compiled in.
$(B)/sf2recomp.ini: sf2recomp.ini
	@mkdir -p "$(B)"
	@test -e "$@" || cp "$<" "$@"

$(EXE): $(RT_OBJ) $(GEN_OBJ) Makefile | check-generated
	@mkdir -p $(B)
	$(file >$(B)/link.rsp,$(foreach obj,$(filter %.o,$^),"$(obj)"))
	$(CXX) -o $@ @$(B)/link.rsp $(LDFLAGS)

# Fast compile for the monolithic functions that cause Clang -O1 optimizer stalls:
SLOW_CHUNKS := $(B)/obj/main/main_047.o $(B)/obj/main/main_183.o $(B)/obj/main/main_662.o \
               $(B)/obj/main/main_704.o $(B)/obj/main/main_735.o $(B)/obj/main/main_746.o \
               $(B)/obj/main/main_759.o \
               $(B)/obj/main/main_048.o $(B)/obj/main/main_188.o $(B)/obj/main/main_526.o \
               $(B)/obj/main/main_673.o $(B)/obj/main/main_715.o $(B)/obj/main/main_770.o \
               $(B)/obj/sdk/sdk_090.o

$(SLOW_CHUNKS): $(B)/obj/%.o: $(GEN)/%.c runtime/cpu.h runtime/simd.h runtime/simd_fp.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O1,$(GENFLAGS)) -O0 -c $< -o $@

$(filter-out $(SLOW_CHUNKS),$(GEN_OBJ)): $(B)/obj/%.o: $(GEN)/%.c runtime/cpu.h runtime/simd.h runtime/simd_fp.h
	@mkdir -p $(dir $@)
	$(CC) $(GENFLAGS) -c $< -o $@

$(B)/rt/%.o: runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -c $< -o $@

$(B)/rt/%.o: runtime/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

runtime-objs: $(RT_OBJ)

.PHONY: android android-prepare
ANDROID_ABI ?= arm64-v8a
android:
	$(PYTHON) tools/build_android.py --abi=$(ANDROID_ABI)
android-prepare:
	$(PYTHON) tools/build_android.py --prepare

clean:
	rm -rf $(B)

# Prepare native dependencies before any runtime compile, including partial-prefix recovery.
.PHONY: runtime-deps
runtime-deps:
	TARGET="$(TARGET)" SDL_DIR="$(SDL_DIR)" PYTHON="$(PYTHON)" bash tools/setup_runtime.sh --ensure
	$(PYTHON) tools/setup_fonts.py --output "$(B)/lib/fonts"

$(RT_OBJ): | runtime-deps

# A source-only Windows gate: no original game data or generated code needed.
.PHONY: windows-smoke
windows-smoke:
	@test "$(TARGET)" = windows || { echo "Use make TARGET=windows windows-smoke in MSYS2 UCRT64" >&2; exit 1; }
	@mkdir -p $(B)
	$(CC) $(RTFLAGS) tests/windows_platform.c runtime/platform/windows.c runtime/paths.c \
	    $(B)/rt/platform/windows_rc.o -o $(B)/windows-platform-test.exe -pthread -lwinmm -lshell32 -ladvapi32 -luser32
	$(B)/windows-platform-test.exe

ifeq ($(TARGET),windows)
windows-smoke: $(B)/rt/platform/windows_rc.o
endif
