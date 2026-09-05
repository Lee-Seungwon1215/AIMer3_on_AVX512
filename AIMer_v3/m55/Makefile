# SPDX-License-Identifier: MIT

PARAM   ?= 128f
BACKEND ?= ref
TEST    ?= reference
MEMORY  ?= bringup
HOST_CC ?= cc
SIGN_SCHEDULE ?= lowmem
BENCH_KERNEL_SAMPLES ?= 31
BENCH_E2E_SAMPLES ?= 7
BENCH_KERNEL_INNER ?= 64
BENCH_WARMUP ?= 1
PROFILE_SAMPLES ?= 31
PROFILE_KERNEL_INNER ?= 64
PROFILE_COMPLEX_INNER ?= 8
STACK_USAGE ?= 0

VALID_PARAMS := 128f 128s 192f 192s 256f 256s
ifeq ($(filter $(PARAM),$(VALID_PARAMS)),)
$(error PARAM must be one of: $(VALID_PARAMS))
endif

VALID_BACKENDS := ref mve
ifeq ($(filter $(BACKEND),$(VALID_BACKENDS)),)
$(error BACKEND must be one of: $(VALID_BACKENDS))
endif

ifeq ($(origin MATVEC),undefined)
ifeq ($(BACKEND),mve)
MATVEC := compact
else
MATVEC := reference
endif
endif

VALID_MATVECS := compact reference mve
ifeq ($(filter $(MATVEC),$(VALID_MATVECS)),)
$(error MATVEC must be one of: $(VALID_MATVECS))
endif
ifeq ($(BACKEND),ref)
ifneq ($(MATVEC),reference)
$(error BACKEND=ref requires MATVEC=reference)
endif
endif

VALID_TESTS := reference sign kat benchmark profile phase phasebench
ifeq ($(filter $(TEST),$(VALID_TESTS)),)
$(error TEST must be one of: $(VALID_TESTS))
endif

VALID_MEMORY := bringup full
ifeq ($(filter $(MEMORY),$(VALID_MEMORY)),)
$(error MEMORY must be one of: $(VALID_MEMORY))
endif

VALID_SIGN_SCHEDULES := lowmem upstream
ifeq ($(filter $(SIGN_SCHEDULE),$(VALID_SIGN_SCHEDULES)),)
$(error SIGN_SCHEDULE must be one of: $(VALID_SIGN_SCHEDULES))
endif

ifeq ($(SIGN_SCHEDULE),lowmem)
LOW_MEMORY_SOURCE := src/ref/sign_low_memory.c src/ref/verify_batch.c
else
LOW_MEMORY_SOURCE :=
endif

include toolchain/arm-none-eabi.mk

AIMER_ROOT := ..
SRC_ROOT := $(AIMER_ROOT)/src
BITS := $(subst f,,$(subst s,,$(PARAM)))
SCHEME_DIR := $(SRC_ROOT)/sig/aimer/reference/aimer-$(PARAM)

ifeq ($(BACKEND),mve)
FIELD_SOURCE := src/mve/field_mve.c
BACKEND_CPPFLAGS := -DAIMER_MVE_BATCH
CONFIG_TAG := mve_compact
BUILD_BACKEND := mve
MVE_MATVEC_SUFFIX :=
ifeq ($(MATVEC),reference)
MATVEC_SOURCE := src/ref/matvec_reference.c
MATVEC_CPPFLAGS := -DAIMER_M55_REFERENCE_MATVEC
CONFIG_TAG := mve_refmat
BUILD_BACKEND := mve-refmat
MVE_MATVEC_SUFFIX := -refmat
else ifeq ($(MATVEC),mve)
MATVEC_SOURCE := src/mve/matvec_mve.c
MATVEC_CPPFLAGS := -DAIMER_M55_MVE_MATVEC
MATVEC_LDFLAGS := \
	-Wl,--wrap=samsungsds_aimer_$(PARAM)_ref_gf_mat_vec_mul_add
CONFIG_TAG := mve_affine
BUILD_BACKEND := mve-affine
MVE_MATVEC_SUFFIX := -mve
else
MATVEC_SOURCE :=
MATVEC_CPPFLAGS :=
endif
else
FIELD_SOURCE := $(SCHEME_DIR)/field$(BITS).c
BACKEND_CPPFLAGS :=
MATVEC_SOURCE :=
MATVEC_CPPFLAGS :=
CONFIG_TAG := ref
BUILD_BACKEND := ref
MVE_MATVEC_SUFFIX :=
endif

BUILD_DIR := build/$(MEMORY)/$(PARAM)/$(BUILD_BACKEND)/$(TEST)
ELF := $(BUILD_DIR)/aimer-m55-$(PARAM)-$(BUILD_BACKEND)-$(TEST).elf
MAP := $(ELF:.elf=.map)
HOST_BUILD_DIR := build/host/$(PARAM)
HOST_TEST := $(HOST_BUILD_DIR)/test-low-memory-$(PARAM)
HOST_MVE_FIELD_TEST := $(HOST_BUILD_DIR)/test-mve-field-$(PARAM)$(MVE_MATVEC_SUFFIX)
HOST_KAT_TEST := $(HOST_BUILD_DIR)/test-kat-$(PARAM)
HOST_MVE_KAT_TEST := $(HOST_BUILD_DIR)/test-kat-mve-$(PARAM)$(MVE_MATVEC_SUFFIX)

MPC_BATCH_SOURCE := src/mve/mpc_batch.c

ifeq ($(TEST),benchmark)
TEST_CPPFLAGS := -DAIMER_BENCHMARK
else ifeq ($(TEST),profile)
TEST_CPPFLAGS := -DAIMER_PHASE_PROFILE
else ifeq ($(TEST),phase)
TEST_CPPFLAGS := -DAIMER_PHASE_PROFILE -DAIMER_BENCHMARK
else ifeq ($(TEST),phasebench)
TEST_CPPFLAGS := -DAIMER_PHASE_PROFILE -DAIMER_BENCHMARK
else
TEST_CPPFLAGS :=
endif

CUBE_N6_DIR ?= /Users/seungwon/STM32CubeN6
CORTEXCTL ?= /Users/Shared/cortexctl
OPENOCD ?= /Users/seungwon/test/.tools/xpack-openocd-0.12.0-7/bin/openocd
OPENOCD_SCRIPTS ?= /Users/seungwon/test/.tools/xpack-openocd-0.12.0-7/openocd/scripts
OPENOCD_TARGET_SCRIPTS ?= /Users/seungwon/test/.tools/openocd-scripts
CMSIS_CORE := $(CUBE_N6_DIR)/Drivers/CMSIS/Include
CMSIS_DEVICE := $(CUBE_N6_DIR)/Drivers/CMSIS/Device/ST/STM32N6xx
HAL_DRIVER := $(CUBE_N6_DIR)/Drivers/STM32N6xx_HAL_Driver
N6_TEMPLATE := $(CUBE_N6_DIR)/Projects/NUCLEO-N657X0-Q/Templates/Template

BOARD_DIR := toolchain/board/nucleo-n657x0-q
ifeq ($(MEMORY),full)
LINKER_SCRIPT := $(BOARD_DIR)/stm32n657-axi-sram.ld
else
LINKER_SCRIPT := $(BOARD_DIR)/stm32n657-sram.ld
endif

CPPFLAGS := \
	$(BACKEND_CPPFLAGS) \
	$(MATVEC_CPPFLAGS) \
	$(TEST_CPPFLAGS) \
	-DPARAMS=$(PARAM) \
	-DAIMER_BACKEND=$(BACKEND) \
	-DAIMER_MATVEC=$(MATVEC) \
	-DAIMER_CONFIG=$(CONFIG_TAG) \
	-DBENCH_KERNEL_SAMPLES=$(BENCH_KERNEL_SAMPLES) \
	-DBENCH_E2E_SAMPLES=$(BENCH_E2E_SAMPLES) \
	-DBENCH_KERNEL_INNER=$(BENCH_KERNEL_INNER) \
	-DBENCH_WARMUP=$(BENCH_WARMUP) \
	-DPROFILE_SAMPLES=$(PROFILE_SAMPLES) \
	-DPROFILE_KERNEL_INNER=$(PROFILE_KERNEL_INNER) \
	-DPROFILE_COMPLEX_INNER=$(PROFILE_COMPLEX_INNER) \
	-DUSE_HAL_DRIVER \
	-DSTM32N657xx \
	-Iinclude \
	-I$(SCHEME_DIR) \
	-I$(SRC_ROOT) \
	-I$(N6_TEMPLATE)/FSBL/Inc \
	-I$(CMSIS_CORE) \
	-I$(CMSIS_DEVICE)/Include \
	-I$(HAL_DRIVER)/Inc \
	-I$(HAL_DRIVER)/Inc/Legacy

CFLAGS := \
	-O3 \
	-std=c11 \
	-fno-common \
	-ffunction-sections \
	-fdata-sections \
	-fno-tree-vectorize \
	-fno-tree-slp-vectorize \
	-MMD -MP \
	--specs=nosys.specs \
	$(COMMON_WARNINGS) \
	$(ARCH_FLAGS)

ifeq ($(STACK_USAGE),1)
CFLAGS += -fstack-usage
endif

LDFLAGS := \
	--specs=nosys.specs \
	-Wl,--gc-sections \
	-Wl,--print-memory-usage \
	-Wl,-Map,$(MAP) \
	-Wl,--wrap=main \
	-Wl,--wrap=_write \
	-Wl,--wrap=_read \
	-T$(LINKER_SCRIPT) \
	$(MATVEC_LDFLAGS) \
	$(ARCH_FLAGS)

C_SOURCES_REFERENCE := \
	tests/test_reference.c \
	src/mve/field_batch.c \
	$(SCHEME_DIR)/field_common.c \
	$(FIELD_SOURCE) \
	$(MATVEC_SOURCE)

C_SOURCES_SIGN := \
	tests/test_sign_verify.c \
	$(LOW_MEMORY_SOURCE) \
	src/mve/field_batch.c \
	$(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c \
	$(SCHEME_DIR)/hash.c \
	$(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c \
	$(SCHEME_DIR)/field_common.c \
	$(FIELD_SOURCE) \
	$(MATVEC_SOURCE) \
	$(SRC_ROOT)/common/aes.c \
	$(SRC_ROOT)/common/fips202.c \
	$(SRC_ROOT)/common/rng.c

C_SOURCES_KAT := \
	tests/kat_runner.c \
	$(LOW_MEMORY_SOURCE) \
	src/mve/field_batch.c \
	$(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c \
	$(SCHEME_DIR)/hash.c \
	$(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c \
	$(SCHEME_DIR)/field_common.c \
	$(FIELD_SOURCE) \
	$(MATVEC_SOURCE) \
	$(SRC_ROOT)/common/aes.c \
	$(SRC_ROOT)/common/fips202.c \
	$(SRC_ROOT)/common/rng.c

C_SOURCES_BENCHMARK := \
	benchmarks/benchmark.c \
	$(LOW_MEMORY_SOURCE) \
	src/mve/field_batch.c \
	$(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c \
	$(SCHEME_DIR)/hash.c \
	$(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c \
	$(SCHEME_DIR)/field_common.c \
	$(FIELD_SOURCE) \
	$(MATVEC_SOURCE) \
	$(SRC_ROOT)/common/aes.c \
	$(SRC_ROOT)/common/fips202.c \
	$(SRC_ROOT)/common/rng.c

C_SOURCES_PROFILE := \
	benchmarks/profile.c \
	src/mve/field_batch.c \
	$(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c \
	$(SCHEME_DIR)/hash.c \
	$(SCHEME_DIR)/field_common.c \
	$(FIELD_SOURCE) \
	$(MATVEC_SOURCE) \
	$(SRC_ROOT)/common/fips202.c

ifeq ($(TEST),benchmark)
C_SOURCES_TEST := $(C_SOURCES_BENCHMARK)
else ifeq ($(TEST),phasebench)
C_SOURCES_TEST := $(C_SOURCES_BENCHMARK)
else ifeq ($(TEST),profile)
C_SOURCES_TEST := $(C_SOURCES_PROFILE)
else ifeq ($(TEST),phase)
C_SOURCES_TEST := $(C_SOURCES_SIGN)
else ifeq ($(TEST),kat)
C_SOURCES_TEST := $(C_SOURCES_KAT)
else ifeq ($(TEST),sign)
C_SOURCES_TEST := $(C_SOURCES_SIGN)
else
C_SOURCES_TEST := $(C_SOURCES_REFERENCE)
endif

ifneq ($(filter $(TEST),sign kat benchmark phase phasebench),)
ifeq ($(SIGN_SCHEDULE),lowmem)
LDFLAGS += \
	-Wl,--wrap=samsungsds_aimer_$(PARAM)_ref_crypto_sign_signature \
	-Wl,--wrap=samsungsds_aimer_$(PARAM)_ref_crypto_sign \
	-Wl,--wrap=samsungsds_aimer_$(PARAM)_ref_crypto_sign_verify \
	-Wl,--wrap=samsungsds_aimer_$(PARAM)_ref_crypto_sign_open
endif
endif

C_SOURCES := \
	$(C_SOURCES_TEST) \
	$(BOARD_DIR)/platform.c \
	$(N6_TEMPLATE)/FSBL/Src/system_stm32n6xx_fsbl.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal_cortex.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal_pwr.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal_pwr_ex.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal_rcc.c \
	$(HAL_DRIVER)/Src/stm32n6xx_hal_rcc_ex.c

ASM_SOURCES := \
	$(N6_TEMPLATE)/STM32CubeIDE/FSBL/Application/Startup/startup_stm32n657x0hxq_fsbl.s

OBJECTS := $(foreach source,$(C_SOURCES),$(BUILD_DIR)/$(notdir $(source)).o)
OBJECTS += $(foreach source,$(ASM_SOURCES),$(BUILD_DIR)/$(notdir $(source)).o)

define compile_c
$(BUILD_DIR)/$(notdir $(1)).o: $(1)
	@mkdir -p $$(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $$< -o $$@
endef

define compile_asm
$(BUILD_DIR)/$(notdir $(1)).o: $(1)
	@mkdir -p $$(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -x assembler-with-cpp -c $$< -o $$@
endef

$(foreach source,$(C_SOURCES),$(eval $(call compile_c,$(source))))
$(foreach source,$(ASM_SOURCES),$(eval $(call compile_asm,$(source))))

.PHONY: all build size disassemble check-mve-config check-mve-disassembly \
	check-mve-affine-disassembly \
	check-low-memory check-mve-field-host check-kat-host check-kat-mve-host \
	run-board clean

all: build

build: $(ELF)

$(ELF): $(OBJECTS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $(OBJECTS) -lc -lm -o $@
	$(SIZE) $@

size: $(ELF)
	$(SIZE) -A $(ELF)

disassemble: $(ELF)
	$(OBJDUMP) -d $(ELF) > $(ELF:.elf=.dis)

check-mve-disassembly: disassemble
	@test "$(BACKEND)" = "mve"
	@rg -q '\bvmullb\.p16\b' $(ELF:.elf=.dis)
	@rg -q '\bvmullt\.p16\b' $(ELF:.elf=.dis)
	@echo "MVE polynomial multiply instructions: VMULLB.P16 + VMULLT.P16"

check-mve-affine-disassembly: disassemble
	@test "$(BACKEND)" = "mve"
	@test "$(MATVEC)" = "mve"
	@$(OBJDUMP) -d --disassemble=m55_gf_mat_vec_mul_add_batch4 $(ELF) \
		>$(ELF:.elf=.affine.dis)
	@rg -q '\bvand\b' $(ELF:.elf=.affine.dis)
	@rg -q '\bveor\b' $(ELF:.elf=.affine.dis)
	@$(NM) $(ELF) | rg -q ' m55_gf_mat_vec_mul_add_batch4$$'
	@$(OBJDUMP) -d --disassemble=m55_aim3_mpc_batch4 $(ELF) | \
		rg -q 'm55_gf_mat_vec_mul(_add)?_batch4'
	@echo "MVE affine instructions: VAND + VEOR; MPC batch call connected"

check-mve-config:
	@printf '' | $(CC) $(ARCH_FLAGS) -dM -E - | grep -q '^#define __ARM_FEATURE_MVE 3$$'
	@echo "MVE compile feature: enabled (__ARM_FEATURE_MVE=3)"

run-board: $(ELF)
	$(CORTEXCTL) run m55 -- $(OPENOCD) \
		-s $(OPENOCD_SCRIPTS) -s $(OPENOCD_TARGET_SCRIPTS) \
		-f $(BOARD_DIR)/openocd.cfg \
		-c init -c 'reset halt' \
		-c 'load_image $(abspath $(ELF))' \
		-c 'verify_image $(abspath $(ELF))' \
		-c 'set vector_msp [mrw 0x34180400]' \
		-c 'set vector_reset [mrw 0x34180404]' \
		-c 'arm semihosting enable' \
		-c 'arm semihosting_resexit enable' \
		-c 'reg msp $$vector_msp' -c 'resume $$vector_reset'

$(HOST_TEST): tests/test_low_memory_host.c src/ref/sign_low_memory.c \
	src/ref/verify_batch.c \
	src/mve/field_batch.c $(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c $(SCHEME_DIR)/hash.c $(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c $(SCHEME_DIR)/field_common.c \
	$(SCHEME_DIR)/field$(BITS).c $(SRC_ROOT)/common/aes.c \
	$(SRC_ROOT)/common/fips202.c $(SRC_ROOT)/common/rng.c
	@mkdir -p $(@D)
	$(HOST_CC) -DPARAMS=$(PARAM) -Iinclude -I$(SCHEME_DIR) -I$(SRC_ROOT) \
		-O2 -std=c11 $^ -o $@

check-low-memory: $(HOST_TEST)
	$(HOST_TEST)

$(HOST_MVE_FIELD_TEST): tests/test_reference.c src/mve/field_mve.c \
	src/mve/field_batch.c $(SCHEME_DIR)/field_common.c $(MATVEC_SOURCE)
	@mkdir -p $(@D)
	$(HOST_CC) -DAIMER_MVE_PORTABLE_TEST $(MATVEC_CPPFLAGS) \
		-DPARAMS=$(PARAM) -DAIMER_MATVEC=$(MATVEC) \
		-Iinclude -I$(SCHEME_DIR) -I$(SRC_ROOT) -O2 -std=c11 $^ -o $@

check-mve-field-host: $(HOST_MVE_FIELD_TEST)
	$(HOST_MVE_FIELD_TEST)

$(HOST_KAT_TEST): tests/kat_runner.c tests/host_platform.c \
	src/ref/sign_low_memory.c src/ref/verify_batch.c \
	src/mve/field_batch.c $(MPC_BATCH_SOURCE) \
	$(SCHEME_DIR)/aim3.c $(SCHEME_DIR)/hash.c $(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c $(SCHEME_DIR)/field_common.c \
	$(SCHEME_DIR)/field$(BITS).c $(SRC_ROOT)/common/aes.c \
	$(SRC_ROOT)/common/fips202.c $(SRC_ROOT)/common/rng.c
	@mkdir -p $(@D)
	$(HOST_CC) -DAIMER_KAT_LOW_MEMORY -DPARAMS=$(PARAM) \
		-Iinclude -I$(SCHEME_DIR) -I$(SRC_ROOT) \
		-O2 -std=c11 $^ -o $@

check-kat-host: $(HOST_KAT_TEST)
	$(HOST_KAT_TEST)

$(HOST_MVE_KAT_TEST): tests/kat_runner.c tests/host_platform.c \
	src/ref/sign_low_memory.c src/ref/verify_batch.c \
	src/mve/field_batch.c $(MPC_BATCH_SOURCE) \
	src/mve/field_mve.c \
	$(MATVEC_SOURCE) \
	$(SCHEME_DIR)/aim3.c $(SCHEME_DIR)/hash.c $(SCHEME_DIR)/sign.c \
	$(SCHEME_DIR)/tree.c $(SCHEME_DIR)/field_common.c \
	$(SRC_ROOT)/common/aes.c $(SRC_ROOT)/common/fips202.c \
	$(SRC_ROOT)/common/rng.c
	@mkdir -p $(@D)
	$(HOST_CC) -DAIMER_KAT_LOW_MEMORY -DAIMER_MVE_PORTABLE_TEST \
		$(MATVEC_CPPFLAGS) -DAIMER_MATVEC=$(MATVEC) \
		-DPARAMS=$(PARAM) -Iinclude -I$(SCHEME_DIR) -I$(SRC_ROOT) \
		-O2 -std=c11 $^ -o $@

check-kat-mve-host: $(HOST_MVE_KAT_TEST)
	$(HOST_MVE_KAT_TEST)

clean:
	rm -rf build

-include $(wildcard $(BUILD_DIR)/*.d)
