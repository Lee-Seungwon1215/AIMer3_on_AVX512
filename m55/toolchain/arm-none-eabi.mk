# SPDX-License-Identifier: MIT

# By default, use the Arm GNU tools available on PATH. Set ARM_GCC_DIR to
# the toolchain's bin directory when they are installed elsewhere.
ARM_GCC_DIR ?=
ARM_GCC_PREFIX := $(if $(strip $(ARM_GCC_DIR)),$(patsubst %/,%,$(ARM_GCC_DIR))/,)

CC      := $(ARM_GCC_PREFIX)arm-none-eabi-gcc
OBJDUMP := $(ARM_GCC_PREFIX)arm-none-eabi-objdump
SIZE    := $(ARM_GCC_PREFIX)arm-none-eabi-size
NM      := $(ARM_GCC_PREFIX)arm-none-eabi-nm

# Do not add -mfpu=fpv5-sp-d16 here.  With GCC 15 it disables MVE code
# generation for Cortex-M55.  The CPU selection already supplies MVE+FP.
ARCH_FLAGS := \
	-mcpu=cortex-m55 \
	-mthumb \
	-mfloat-abi=hard \
	-mcmse

COMMON_WARNINGS := \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wconversion \
	-Wstrict-prototypes
