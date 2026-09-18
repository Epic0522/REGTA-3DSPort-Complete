# Check the compiler, not a private checkout path. Source-built r55 is valid.
REGTA_GCC_VERSION := $(shell "$(DEVKITARM)/bin/arm-none-eabi-gcc" -dumpfullversion 2>/dev/null)
REGTA_GCC_RELEASE := $(shell "$(DEVKITARM)/bin/arm-none-eabi-gcc" --version 2>/dev/null | head -1)
ifneq ($(REGTA_GCC_VERSION),10.2.0)
$(error REGTA requires devkitARM r55 / GCC 10.2.0; see README Linux and macOS source-build instructions)
endif
ifeq ($(findstring devkitARM release 55,$(REGTA_GCC_RELEASE)),)
$(error This is not devkitARM release 55; generic ARM GCC 10.2 is not a substitute)
endif
