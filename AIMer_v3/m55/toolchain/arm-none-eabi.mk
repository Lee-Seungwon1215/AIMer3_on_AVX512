# SPDX-License-Identifier: MIT

# The toolchain can be overridden by passing ARM_GCC_DIR on the make command
# line.  The default is the locally installed xPack GCC used for this port.
ARM_GCC_DIR ?= /Users/seungwon/test/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin

CC      := $(ARM_GCC_DIR)/arm-none-eabi-gcc
OBJDUMP := $(ARM_GCC_DIR)/arm-none-eabi-objdump
SIZE    := $(ARM_GCC_DIR)/arm-none-eabi-size
NM      := $(ARM_GCC_DIR)/arm-none-eabi-nm

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
