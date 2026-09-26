ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not in your environment.")
endif

include ../../common/devkitarm-r55.mk


include $(DEVKITARM)/3ds_rules

ifneq ($(strip $(STEREO)),)
$(error "Stereo re3 builds are disabled on 3DS; all production audio must be mono")
endif
TARGET	:=re3
LIBRW	:=librw

#RE3_CFG		:=-DAUDIO_OAL -D_3DS_PERFCOUNT -DLIBRW -DRW_3DS
# Full lower-screen presentation is the normal re3 3DS configuration. Earlier
# boot failures were GCC 15 builds; the bundled r55 toolchain is mandatory.
BOTTOM_LOADING	?=1
BOTTOM_RADAR	?=1
TXD_CDIMAGE	?=1
EMULATOR_SKIP_OP ?=0
ifeq ($(EMULATOR_SKIP_OP),1)
	TARGET := re3-azahar-noop
endif
RE3_CFG		:=-DAUDIO_OAL -D_3DS_PERFCOUNT -DLIBRW -DRW_3DS -DMASTER
ifeq ($(TXD_CDIMAGE),1)
	RE3_CFG += -DUSE_TXD_CDIMAGE
endif
ifeq ($(BOTTOM_LOADING),1)
	RE3_CFG += -DENABLE_3DS_BOTTOM_LOADING
endif
ifeq ($(BOTTOM_RADAR),1)
	RE3_CFG += -DENABLE_3DS_BOTTOM_RADAR
endif
ifeq ($(EMULATOR_SKIP_OP),1)
	RE3_CFG += -DEMULATOR_SKIP_STARTUP_MOVIES
endif
LIBRW_CFG	:=-DRW_3DS -DRE3_3DS_BUILD -DRW_3DS_BUFFERED_IM3D
MPG123_CFG	:=-DOPT_MULTI -DOPT_GENERIC -DOPT_GENERIC_DITHER -DOPT_NEON -DREAL_IS_FLOAT
DEBUG		:=-g
WISDOM		:= # -Wall -Werr (yeah right)

#Debug
#OPTFLAGS	:=-O2

#Release
#OPTFLAGS	:=-O3 -flto -fwhole-program
OPTFLAGS	:=-O4 -flto -fwhole-program -DNDEBUG

# If you want to swap between build configurations uncomment.
# The next two lines and comment PREFX :=./obj/.
# each set of object files will be associated with the fingerprint.

FPSTR		:="$(RE3_CFG)$(LIBRW_CFG)$(MPG123_CFG)$(DEBUG)$(OPTFLAGS)$(WISDOM)"
FINGERPRINT	:=$(shell echo "$(FPSTR)" | md5sum | cut -d' ' -f1)
PREFIX		:=./obj/$(FINGERPRINT)/

#PREFIX		:=./obj/

# re3

RE3_SRC		:=	../src/animation \
			../src/audio     \
			../src/audio/oal \
			../src/audio/eax \
			../src/buildings \
			../src/collision \
			../src/control   \
			../src/core      \
			../src/entities  \
			../src/extras    \
			../src/fakerw    \
			../src/math      \
			../src/modelinfo \
			../src/objects   \
			../src/peds      \
			../src/render    \
			../src/rw        \
			../src/save      \
			../src/skel      \
			../src/skel/3ds  \
			../src/text      \
			../src/vehicles  \
			../src/weapons

RE3_PRV_INC	:=	$(foreach dir,$(RE3_SRC),-I$(dir))

# librw

LIBRW_SRC	:=	../vendor/$(LIBRW)             \
			../vendor/$(LIBRW)/src         \
			../vendor/$(LIBRW)/src/3ds     \
			../vendor/$(LIBRW)/src/3ds/tex \
			../vendor/$(LIBRW)/src/d3d     \
			../vendor/$(LIBRW)/src/gl      \
			../vendor/$(LIBRW)/src/ps2     \
			../vendor/$(LIBRW)/src/lodepng

LIBRW_INC	:=	$(foreach dir,$(LIBRW_SRC),-I$(dir))

# libctru

LIBCTRU_SRC	:=	../vendor/libctru/source                 \
			../vendor/libctru/source/allocator       \
			../vendor/libctru/source/gpu             \
			../vendor/libctru/source/ndsp            \
			../vendor/libctru/source/services        \
			../vendor/libctru/source/services/soc    \
			../vendor/libctru/source/applets         \
			../vendor/libctru/source/util/decompress \
			../vendor/libctru/source/util/rbtree     \
			../vendor/libctru/source/util/utf        \
			../vendor/libctru/source/system

LIBCTRU_PRV_INC	:=	$(foreach dir,$(LIBCTRU_SRC),-I$(dir))

LIBCTRU_INC	:=	-I../vendor/libctru/include

# citro3d

CITRO3D_SRC	:=	../vendor/citro3d/source \
			../vendor/citro3d/source/maths

CITRO3D_PRV_INC	:=	$(foreach dir,$(CITRO3D_SRC),-I$(dir))

CITRO3D_INC	:=	-I../vendor/citro3d/include

# libmpg123

MPG123_SSRC	:=	../vendor/mpg123-ctr/src/libmpg123/synth_arm.S                  \
			../vendor/mpg123-ctr/src/libmpg123/synth_arm_accurate.S         \
			../vendor/mpg123-ctr/src/libmpg123/synth_neon.S                 \
			../vendor/mpg123-ctr/src/libmpg123/synth_neon_accurate.S        \
			../vendor/mpg123-ctr/src/libmpg123/synth_neon_float.S           \
			../vendor/mpg123-ctr/src/libmpg123/synth_neon_s32.S             \
			../vendor/mpg123-ctr/src/libmpg123/synth_stereo_neon.S          \
			../vendor/mpg123-ctr/src/libmpg123/synth_stereo_neon_s32.S      \
			../vendor/mpg123-ctr/src/libmpg123/synth_stereo_neon_float.S    \
			../vendor/mpg123-ctr/src/libmpg123/synth_stereo_neon_accurate.S \
			../vendor/mpg123-ctr/src/libmpg123/dct36_neon.S                 \
			../vendor/mpg123-ctr/src/libmpg123/dct64_neon.S                 \
			../vendor/mpg123-ctr/src/libmpg123/dct64_neon_float.S           \

MPG123_CSRC	:=	../vendor/mpg123-ctr/src/compat/compat.c             \
			../vendor/mpg123-ctr/src/compat/compat_str.c         \
			../vendor/mpg123-ctr/src/compat/cpuflags_3ds.c       \
			../vendor/mpg123-ctr/src/libmpg123/dct64.c           \
			../vendor/mpg123-ctr/src/libmpg123/dither.c          \
			../vendor/mpg123-ctr/src/libmpg123/feature.c         \
			../vendor/mpg123-ctr/src/libmpg123/format.c          \
			../vendor/mpg123-ctr/src/libmpg123/frame.c           \
			../vendor/mpg123-ctr/src/libmpg123/id3.c             \
			../vendor/mpg123-ctr/src/libmpg123/index.c           \
			../vendor/mpg123-ctr/src/libmpg123/layer1.c          \
			../vendor/mpg123-ctr/src/libmpg123/layer2.c          \
			../vendor/mpg123-ctr/src/libmpg123/layer3.c          \
			../vendor/mpg123-ctr/src/libmpg123/libmpg123.c       \
			../vendor/mpg123-ctr/src/libmpg123/ntom.c            \
			../vendor/mpg123-ctr/src/libmpg123/optimize.c        \
			../vendor/mpg123-ctr/src/libmpg123/parse.c           \
			../vendor/mpg123-ctr/src/libmpg123/readers.c         \
			../vendor/mpg123-ctr/src/libmpg123/stringbuf.c       \
			../vendor/mpg123-ctr/src/libmpg123/synth.c           \
			../vendor/mpg123-ctr/src/libmpg123/synth_8bit.c      \
			../vendor/mpg123-ctr/src/libmpg123/synth_s32.c       \
			../vendor/mpg123-ctr/src/libmpg123/synth_real.c      \
			../vendor/mpg123-ctr/src/libmpg123/tabinit.c

MPG123_PRV_INC	:=	-I../vendor/mpg123-ctr/src           \
			-I../vendor/mpg123-ctr/src/compat    \
			-I../vendor/mpg123-ctr/src/libmpg123

MPG123_INC	:=	-I../vendor/mpg123-ctr/src/include \
			-I../vendor/mpg123-ctr/src/libmpg123

# openal

OAL_SRC		:=	$(wildcard ../vendor/openal-soft-ctr/Alc/*.c)      \
			$(wildcard ../vendor/openal-soft-ctr/OpenAL32/*.c) \
			../vendor/openal-soft-ctr/Alc/backends/null.c      \
			../vendor/openal-soft-ctr/Alc/backends/loopback.c  \
			../vendor/openal-soft-ctr/Alc/backends/ctr.c

OAL_PRV_INC	:=	-I../vendor/openal-soft-ctr              \
			-I../vendor/openal-soft-ctr/include      \
			-I../vendor/openal-soft-ctr/Alc          \
			-I../vendor/openal-soft-ctr/Alc/backends \
			-I../vendor/openal-soft-ctr/OpenAL32/Include

OAL_INC		:=	-I../vendor/openal-soft-ctr/include

DATA_DIRS	:=	../vendor/libctru/data
ifeq ($(BOTTOM_RADAR),1)
	DATA_DIRS += ../src/core
endif

SOURCE_DIRS	:=	$(LIBCTRU_SRC) $(CITRO3D_SRC) $(LIBRW_SRC) $(RE3_SRC)

# Source files / Object files

GIT_SHA1_CPP	:=	../src/extras/GitSHA1.cpp
CPPFILES	:=	$(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.cpp))
CPPFILES	:=	$(filter-out $(GIT_SHA1_CPP),$(CPPFILES)) $(GIT_SHA1_CPP)
CFILES		:=	$(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.c))
CFILES		+=	$(OAL_SRC)
CFILES		+=	$(MPG123_CSRC)

SFILES		:=	$(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.s))
SFILES		+=	$(MPG123_SSRC)

PICAFILES	:=	$(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.shlist))
DATAFILES	:=	$(foreach dir,$(DATA_DIRS),$(wildcard $(dir)/*.bin))

OBJECTS		:=	$(foreach file,$(SFILES),$(patsubst ../%,$(PREFIX)%,$(basename $(file)).o))
OBJECTS		+=	$(foreach file,$(CFILES),$(patsubst ../%,$(PREFIX)%,$(basename $(file)).o))
OBJECTS		+=	$(foreach file,$(CPPFILES),$(patsubst ../%,$(PREFIX)%,$(basename $(file)).o))

PICA_OBJ	:=	$(foreach file,$(PICAFILES),$(patsubst ../%,$(PREFIX)%,$(patsubst %.shlist,%.shbin.o,$(file))))
DATA_OBJ	:=	$(foreach file,$(DATAFILES),$(patsubst ../%,$(PREFIX)%,$(patsubst %.bin,%.bin.o,$(file))))

PICA_INC	:=	$(sort $(foreach obj,$(PICA_OBJ),-I$(dir $(obj))))
DATA_INC	:=	$(sort $(foreach obj,$(DATA_OBJ),-I$(dir $(obj))))

# code generation

ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
ASFLAGS		:=	$(DEBUG) $(ARCH)

CFLAGS		:=	$(WISDOM) $(DEBUG) $(OPTFLAGS) $(ARCH)  \
			-mword-relocations -ffunction-sections  \
			-DARM11 -D_3DS \
			$(LIBCTRU_INC) -I$(PORTLIBS)/include

LIBS		:=	-lm

LDFLAGS		:=	$(DEBUG) $(ARCH) $(OPTFLAGS) \
			-specs=3dsx.specs -Wl,--allow-multiple-definition \
			-Wl,-Map,$(notdir $*.map)

CXXFLAGS	:=	$(CFLAGS) \
			-std=gnu++11 -fno-rtti -fno-exceptions -fpermissive

DEPGEN		=	-MMD -MP -MF $@.d

DEPS		:=	$(foreach obj,$(OBJECTS),$(obj).d)  \
			$(foreach obj,$(PICA_OBJ),$(obj).d) \
			$(foreach obj,$(DATA_OBJ),$(obj).d)

# rules
default: $(TARGET).3dsx

.PHONY: force-git-sha1
force-git-sha1:

$(GIT_SHA1_CPP): force-git-sha1
	@bash ../printHash.sh $@


# binary files
$(PREFIX)%.bin.o $(PREFIX)%_bin.h: ../%.bin
	@echo $(notdir $<)
	@mkdir -p $(dir $@) || true
	@echo $@ $(patsubst %.bin.o,%_bin.h,$@): $< > $@.d
	@bin2s -a 4 -H $(patsubst %.bin.o,%_bin.h,$@) $< | $(AS) -o $@

# compiling libctru *.cpp
$(PREFIX)vendor/libctru/%.o: ../vendor/libctru/%.cpp
	@mkdir -p $(dir $@) || true
	@echo $(CXX) $<
	@$(CXX) $(DEPGEN) -c $< -o $@ $(CXXFLAGS) $(LIBCTRU_PRV_INC) $(DATA_INC)

# compiling libctru *.c
$(PREFIX)vendor/libctru/%.o: ../vendor/libctru/%.c
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) $(DEPGEN) -c $< -o $@ $(CFLAGS) $(LIBCTRU_PRV_INC) $(DATA_INC)

# libctru asm
$(PREFIX)vendor/libctru/%.o: ../vendor/libctru/%.s
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) $(DEPGEN) -x assembler-with-cpp -c $< -o $@ $(LIBCTRU_INC)

# compiling citro3d *.c
$(PREFIX)vendor/citro3d/%.o: ../vendor/citro3d/%.c
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) -c $< $(DEPGEN) -o $@ $(CFLAGS) $(CITRO3D_PRV_INC) $(CITRO3D_INC)

# mpg123 *.c
$(PREFIX)vendor/mpg123-ctr/%.o: ../vendor/mpg123-ctr/%.c
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) -c $< $(DEPGEN) -o $@ -ffast-math $(CFLAGS) $(MPG123_CFG) $(MPG123_PRV_INC)

# mpg123 asm
$(PREFIX)vendor/mpg123-ctr/%.o: ../vendor/mpg123-ctr/%.S
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) $(DEPGEN) -c $< -o $@ $(MPG123_CFG) $(MPG123_PRV_INC)

# compiling OAL
$(PREFIX)vendor/openal-soft-ctr/%.o: ../vendor/openal-soft-ctr/%.c
	@mkdir -p $(dir $@) || true
	@echo $(CC) $<
	@$(CC) -c $< $(DEPGEN) -o $@ $(CFLAGS) $(OAL_PRV_INC)

# compiling librw
$(PREFIX)vendor/$(LIBRW)/%.o: ../vendor/$(LIBRW)/%.cpp
	@mkdir -p $(dir $@) || true
	@echo $(CXX) $<
	@$(CXX) $(DEPGEN) -c $< -o $@ $(CXXFLAGS) $(LIBRW_CFG) $(CITRO3D_INC) $(LIBRW_INC) $(PICA_INC)

# compiling shaders
$(PREFIX)%.shbin.o $(PREFIX)%_shbin.h: ../%.shlist
	@mkdir -p $(dir $@) || true
	@./shlist.sh $< $@

# compiling re3
$(PREFIX)src/%.o: ../src/%.cpp
	@mkdir -p $(dir $@) || true
	@echo $(CXX) $<
	@$(CXX) $(DEPGEN) -c $< -o $@ $(CXXFLAGS) $(RE3_CFG) $(RE3_PRV_INC) $(CITRO3D_INC) $(LIBRW_INC) $(OAL_INC) $(MPG123_INC) $(DATA_INC)

# random asm
$(PREFIX)%.o: ../%.s
	@mkdir -p $(dir $@) || true
	@echo Ignoring assembly file: $<
	@echo | $(AS) -o $@

# linking
%.elf:
	@echo linking $(notdir $@)
	@$(CXX) $(LDFLAGS) $(PICA_OBJ) $(DATA_OBJ) $(OBJECTS) $(LIBS) -o $@
	@$(NM) -CSn $@ > $(notdir $*.lst)

$(OBJECTS): | $(PICA_OBJ) $(DATA_OBJ)

$(TARGET).3dsx: $(TARGET).elf

$(TARGET).elf: $(OBJECTS) $(OBJ_PICA) $(OBJ_DATA) $(PREFIX)info

$(PREFIX)info:
	@echo 'FINGERPRINT:\t'$(FINGERPRINT)  > $@
	@echo 'RE3_CFG:    \t'$(RE3_CFG)     >> $@
	@echo 'LIBRW_CFG:  \t'$(LIBRW_CFG)   >> $@
	@echo 'DEBUG:      \t'$(DEBUG)       >> $@
	@echo 'OPTFLAGS:   \t'$(OPTFLAGS)    >> $@
	@echo 'WISDOM:     \t'$(WISDOM)      >> $@

.PHONY: clean
clean:
	@rm -f         \
	$(TARGET).3dsx \
	$(TARGET).elf  \
	$(OBJECTS)     \
	$(OBJ_PICA)    \
	$(OBJ_DATA)    \
	$(DEPS)

.PHONY: copy
copy:
	@true

.PHONY: upload
upload:
	3dslink -a 10.0.0.2 $(TARGET).3dsx

.PHONY: debug
debug:
	$(DEVKITARM)/bin/arm-none-eabi-gdb $(TARGET).elf

-include $(DEPS)
