# SF2 Recomp build (the runtime is LAS Recomp's MIT core; see licenses/LASRecomp.txt)
CC  := clang
CXX := clang++
.DEFAULT_GOAL := all
B       ?= build
GEN     ?= game/gen
GAME    ?= game/extracted/update
PYTHON  ?= python3
SDL_DIR ?= $(CURDIR)/deps
# Default to the local prefix that runtime-deps can prepare. Prefer a suitable
# system SDL only when there is no complete local installation.
SDL_CFLAGS := -I$(SDL_DIR)/include
SDL_LIBS := -L$(SDL_DIR)/lib -lSDL3 -Wl,-rpath,$(SDL_DIR)/lib
ifeq ($(and $(wildcard $(SDL_DIR)/include/SDL3/SDL.h),$(wildcard $(SDL_DIR)/lib/libSDL3.*)),)
ifeq ($(shell pkg-config --atleast-version=3.4.0 sdl3 2>/dev/null && echo yes),yes)
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
endif
endif

# The generated game code (tools/recompr/gen.py), compiled like the generator expects:
GENFLAGS := -O1 -mcx16 -fno-strict-aliasing -Wno-return-type -Wno-unused-label -fvisibility=hidden -fPIC -Iruntime
RTFLAGS  := -O2 -g -mcx16 -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format \
            -Iruntime -Iruntime/nvn -Ithird_party/smaa -Ithird_party/smaa/Textures $(SDL_CFLAGS) -MMD -MP
CXXFLAGS := -O2 -g -mcx16 -std=c++17 -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format \
            -Iruntime -Iruntime/nvn -Ithird_party/smaa -Ithird_party/smaa/Textures $(SDL_CFLAGS) -MMD -MP

SHADERC  ?= $(shell pkg-config --libs shaderc 2>/dev/null || echo -lshaderc)
LDFLAGS  := -fuse-ld=lld -rdynamic -Wl,-rpath,'$$ORIGIN/lib' $(SDL_LIBS) -lvulkan $(SHADERC) -latomic -pthread -lm -ldl

EXE := $(B)/sf2

# Download pinned SMAA sources only when absent; generated files stay out of Git.
SMAA_INPUTS := third_party/smaa/smaa_hlsl.inc third_party/smaa/Textures/AreaTex.h third_party/smaa/Textures/SearchTex.h
$(SMAA_INPUTS) &: tools/setup_smaa.py | runtime-deps
	$(PYTHON) tools/setup_smaa.py
	@touch $(SMAA_INPUTS)

$(B)/rt/nvn/smaa_data.o: $(SMAA_INPUTS)

GEN_SRC := $(wildcard $(GEN)/main/*.c) $(wildcard $(GEN)/sdk/*.c)
RT_SRC  := $(wildcard runtime/*.c) $(wildcard runtime/hle/*.c) $(wildcard runtime/services/*.c) \
           $(wildcard runtime/nvn/*.c) $(wildcard runtime/nvn/maxwell/*.c) $(wildcard runtime/nvn/vulkan/*.c) \
           $(wildcard runtime/ui/*.c) runtime/platform/linux.c
RT_CXX_SRC := $(wildcard runtime/nvn/vulkan/*.cpp)

GEN_OBJ := $(patsubst $(GEN)/%.c,$(B)/obj/%.o,$(GEN_SRC))
RT_OBJ  := $(patsubst runtime/%.c,$(B)/rt/%.o,$(RT_SRC)) $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(RT_CXX_SRC))
DEPS    := $(RT_OBJ:.o=.d)

all: $(EXE) $(B)/sf2recomp.ini

.PHONY: all gen check-generated runtime-objs clean
gen:
	@test -f "$(GAME)/exefs/main" -a -f "$(GAME)/exefs/sdk" || { echo "Extract the supported game first: tools/extract.sh NSP KEYS" >&2; exit 1; }
	@test -f game/il2cpp/sym_main.txt || { echo "Generate the symbol map first: tools/il2cpp_run.sh $(GAME)" >&2; exit 1; }
	$(PYTHON) tools/recompr/gen.py main "$(GAME)/exefs/main" 0x7000000000 "$(GEN)/main" --symbols game/il2cpp/sym_main.txt
	$(PYTHON) tools/recompr/gen.py sdk "$(GAME)/exefs/sdk" 0x7004000000 "$(GEN)/sdk"

check-generated:
	@grep -q main_nso_build_id "$(GEN)/main/main_table.c" 2>/dev/null && grep -q sdk_nso_build_id "$(GEN)/sdk/sdk_table.c" 2>/dev/null || { echo "Generated code is missing/outdated. Run make gen or tools/convert.py first." >&2; exit 1; }

# Preserve runtime edits on subsequent builds. The file is never compiled in.
$(B)/sf2recomp.ini: sf2recomp.ini
	@mkdir -p "$(B)"
	@test -e "$@" || cp "$<" "$@"

$(EXE): $(RT_OBJ) $(GEN_OBJ) Makefile | check-generated
	@mkdir -p $(B)
	$(CXX) -o $@ $(filter %.o,$^) $(LDFLAGS)

# Fast compile for the monolithic functions that cause Clang -O1 optimizer stalls:
SLOW_CHUNKS := $(B)/obj/main/main_047.o $(B)/obj/main/main_183.o $(B)/obj/main/main_662.o \
               $(B)/obj/main/main_704.o $(B)/obj/main/main_735.o $(B)/obj/main/main_746.o \
               $(B)/obj/main/main_759.o

$(SLOW_CHUNKS): $(B)/obj/%.o: $(GEN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O1,$(GENFLAGS)) -O0 -c $< -o $@

$(filter-out $(SLOW_CHUNKS),$(GEN_OBJ)): $(B)/obj/%.o: $(GEN)/%.c
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

clean:
	rm -rf $(B)

# Prepare native dependencies before any runtime compile, including partial-prefix recovery.
.PHONY: runtime-deps
runtime-deps:
	SDL_DIR="$(SDL_DIR)" tools/setup_runtime.sh --ensure

$(RT_OBJ): | runtime-deps
