# ==== Toolchain ====
CC      := /usr/bin/cc
AR      := /usr/bin/ar
RM      := rm -rf
MKDIR_P := mkdir -p

# ==== Layout ====
BUILD := build
SRC   := src
INC   := include
OUT   := out

# ==== Common flags ====
DEPFLAGS        := -MMD -MP
CFLAGS_INTERNAL := -fPIC -fvisibility=hidden -Wa,--noexecstack -O3 -fomit-frame-pointer -fdata-sections -ffunction-sections -Wl,--gc-sections -Wbad-function-cast -std=gnu11
CFLAGS_RANDNIST := -fPIC -fvisibility=default -Wa,--noexecstack -O3 -fomit-frame-pointer -fdata-sections -ffunction-sections -Wl,--gc-sections -Wbad-function-cast -std=gnu11
OQS_DEFS := -DOQS_HAVE_ALIGNED_ALLOC -DOQS_HAVE_EXPLICIT_BZERO -DOQS_HAVE_MEMALIGN -DOQS_HAVE_POSIX_MEMALIGN -DOQS_DIST_BUILD -DOQS_DIST_X86_64_BUILD
# Per-implementation SIMD flags for the AIMer optimized kernels
AVX2_FLAGS    := -mavx2 -mpclmul -mbmi2 -mpopcnt -maes
AVX512_FLAGS  := -mavx2 -mpclmul -mavx512f -mavx512vl -mavx512bw -mavx512dq -mvpclmulqdq -mbmi2 -mpopcnt -maes

# ==== Convenience ====
INCS := -I./$(INC)

# ==== Default target ====
.PHONY: all clean distclean
all: $(BUILD)/lib/liboqs-internal.a $(BUILD)/lib/liboqs.a $(BUILD)/tests/kat_sig $(BUILD)/tests/bench_sig $(BUILD)/tests/bench_full $(BUILD)/tests/bench_full_paper

# ==== Directories ====
DIRS := \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/plain-64bits \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/serial \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/avx2 \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/avx2 \
  $(BUILD)/common/sha3/avx512vl_low \
  $(BUILD)/common/aes \
  $(BUILD)/common/sha2 \
  $(BUILD)/common/sha3 \
  $(BUILD)/common \
  $(BUILD)/common/pqclean_shims \
  $(BUILD)/common/rand \
  $(BUILD)/lib \
  $(BUILD)/sig \
  $(BUILD)/sig/aimer \
  $(foreach v,128f 128s 192f 192s 256f 256s,$(foreach i,ref avx2 avx512,$(BUILD)/sig/aimer/$(v)_$(i))) \
  $(BUILD)/tests

$(DIRS):
	$(MKDIR_P) $@

# ==== Object lists ====

OBJ_KECCAK := \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/plain-64bits/KeccakP-1600-opt64.o \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/serial/KeccakP-1600-times4-on1.o

OBJ_AES := \
  $(BUILD)/common/aes/aes_impl.o \
  $(BUILD)/common/aes/aes_c.o \
  $(BUILD)/common/aes/aes.o

OBJ_SHA2 := $(BUILD)/common/sha2/sha2.o

# Intel AVX512VL Keccak (from liboqs 0.15.0): whole-sponge x1+x4. Selected at
# runtime by OQS_init() when the CPU has AVX-512.
OBJ_SHA3_AVX512VL := \
  $(BUILD)/common/sha3/avx512vl_sha3.o \
  $(BUILD)/common/sha3/avx512vl_sha3x4.o \
  $(BUILD)/common/sha3/avx512vl_low/KeccakP-1600-AVX512VL.o \
  $(BUILD)/common/sha3/avx512vl_low/KeccakP-1600-times4-AVX512VL.o \
  $(BUILD)/common/sha3/avx512vl_low/SHA3-AVX512VL.o \
  $(BUILD)/common/sha3/avx512vl_low/SHA3-times4-AVX512VL.o

# AVX2 Keccak (XKCP, from liboqs 0.15.0): a 2nd xkcp backend forced to AVX2 with
# renamed callbacks (sha3_avx2_callbacks / sha3_x4_avx2_callbacks). Used only by
# the bench/KAT harness to run _avx2 with AVX2 SHAKE (see tests/).
OBJ_SHA3_AVX2 := \
  $(BUILD)/common/sha3/avx2_sha3.o \
  $(BUILD)/common/sha3/avx2_sha3x4.o \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/avx2/KeccakP-1600-AVX2.o \
  $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/avx2/KeccakP-1600-times4-SIMD256.o

OBJ_SHA3 := \
  $(BUILD)/common/sha3/xkcp_sha3.o \
  $(BUILD)/common/sha3/xkcp_sha3x4.o \
  $(BUILD)/common/sha3/sha3.o \
  $(BUILD)/common/sha3/sha3x4.o \
  $(OBJ_SHA3_AVX2) \
  $(OBJ_SHA3_AVX512VL)

OBJ_COMMON := $(BUILD)/common/common.o

OBJ_PQCLEAN_SHIMS := \
  $(BUILD)/common/pqclean_shims/fips202.o \
  $(BUILD)/common/pqclean_shims/fips202x4.o

OBJ_RAND := \
  $(BUILD)/common/rand/rand.o \
  $(BUILD)/common/rand/rand_nist.o

OBJ_SIG_GLUE := $(BUILD)/sig/sig.o

# -- aimer (ref + avx2 + avx512 implementations, runtime-dispatched) --
AIMER_VARIANTS := 128f 128s 192f 192s 256f 256s
AIMER_IMPLS    := ref avx2 avx512

# Per-(variant,impl) field object name: ref uses field<sz>.o; avx2/avx512 use field.o
aimer_field_obj = $(if $(filter ref,$(2)),field$(patsubst %f,%,$(patsubst %s,%,$(1))),field)
# Per-(variant,impl) kernel objects
AIMER_IMPL_OBJS = \
  $(BUILD)/sig/aimer/$(1)_$(2)/aim2.o \
  $(BUILD)/sig/aimer/$(1)_$(2)/hash.o \
  $(BUILD)/sig/aimer/$(1)_$(2)/sign.o \
  $(BUILD)/sig/aimer/$(1)_$(2)/tree.o \
  $(BUILD)/sig/aimer/$(1)_$(2)/$(call aimer_field_obj,$(1),$(2)).o

# Per-variant HASH_PREFIX_0 (KS domain separator) for the optimized variants' hash.h
PFX_128f := 0x00
PFX_128s := 0x10
PFX_192f := 0x20
PFX_192s := 0x30
PFX_256f := 0x40
PFX_256s := 0x50

OBJ_AIMER := \
  $(foreach v,$(AIMER_VARIANTS),$(foreach i,$(AIMER_IMPLS),$(call AIMER_IMPL_OBJS,$(v),$(i)))) \
  $(foreach v,$(AIMER_VARIANTS),$(BUILD)/sig/aimer/sig_aimer_$(v).o)

# ==== Internal lib ====
INTLIB := $(BUILD)/lib/liboqs-internal.a
$(INTLIB): $(DIRS) $(OBJ_KECCAK) $(OBJ_AES) $(OBJ_SHA2) $(OBJ_SHA3) $(OBJ_COMMON) $(OBJ_RAND)
	$(AR) rcs $@ \
	  $(OBJ_KECCAK) \
	  $(OBJ_AES) \
	  $(OBJ_SHA2) \
	  $(OBJ_SHA3) \
	  $(OBJ_COMMON) \
	  $(BUILD)/common/rand/rand_nist.o

# ==== Public lib ====
PUBLIB := $(BUILD)/lib/liboqs.a
$(PUBLIB): $(DIRS) $(OBJ_PQCLEAN_SHIMS) $(OBJ_SIG_GLUE) $(OBJ_AIMER)
	$(AR) rcs $@ \
	  $(OBJ_PQCLEAN_SHIMS) \
	  ./$(BUILD)/sig/sig.o \
	  $(OBJ_AIMER) \
	  ./$(BUILD)/common/rand/rand.o

# ==== Tests ====
$(BUILD)/tests/kat_sig: $(DIRS) $(BUILD)/tests/kat_sig.o $(BUILD)/tests/test_helpers.o $(BUILD)/tests/aimer_keccak_select.o $(PUBLIB) $(INTLIB)
	$(CC) -Wl,-z,noexecstack $(BUILD)/tests/kat_sig.o $(BUILD)/tests/test_helpers.o $(BUILD)/tests/aimer_keccak_select.o -o $@ $(PUBLIB) $(INTLIB)

$(BUILD)/tests/bench_sig: $(DIRS) $(BUILD)/tests/bench_sig.o $(BUILD)/tests/aimer_keccak_select.o $(PUBLIB) $(INTLIB)
	$(CC) -Wl,-z,noexecstack $(BUILD)/tests/bench_sig.o $(BUILD)/tests/aimer_keccak_select.o -o $@ $(PUBLIB) $(INTLIB)

$(BUILD)/tests/bench_full: $(DIRS) $(BUILD)/tests/bench_full.o $(BUILD)/tests/aimer_keccak_select.o $(PUBLIB) $(INTLIB)
	$(CC) -Wl,-z,noexecstack $(BUILD)/tests/bench_full.o $(BUILD)/tests/aimer_keccak_select.o -o $@ $(PUBLIB) $(INTLIB) -lm

$(BUILD)/tests/bench_full_paper: $(DIRS) $(BUILD)/tests/bench_full_paper.o $(BUILD)/tests/aimer_keccak_select.o $(PUBLIB) $(INTLIB)
	$(CC) -Wl,-z,noexecstack $(BUILD)/tests/bench_full_paper.o $(BUILD)/tests/aimer_keccak_select.o -o $@ $(PUBLIB) $(INTLIB) -lm

# ==== Compile rules ====

# --- keccak xkcp low ---
$(BUILD)/common/sha3/xkcp_low/KeccakP-1600/plain-64bits/KeccakP-1600-opt64.o: $(SRC)/common/sha3/xkcp_low/KeccakP-1600/plain-64bits/KeccakP-1600-opt64.c | $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/plain-64bits
	$(CC) -DADD_SYMBOL_SUFFIX $(INCS) -I./src/common/sha3/xkcp_low/KeccakP-1600/plain-64bits $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/serial/KeccakP-1600-times4-on1.o: $(SRC)/common/sha3/xkcp_low/KeccakP-1600times4/serial/KeccakP-1600-times4-on1.c | $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/serial
	$(CC) -DADD_SYMBOL_SUFFIX $(INCS) -I./src/common/sha3/xkcp_low/KeccakP-1600times4/serial $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# --- aes / sha2 / sha3 / common ---
$(BUILD)/common/aes/%.o: $(SRC)/common/aes/%.c | $(BUILD)/common/aes
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

$(BUILD)/common/sha2/%.o: $(SRC)/common/sha2/%.c | $(BUILD)/common/sha2
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

$(BUILD)/common/sha3/xkcp_sha3.o: $(SRC)/common/sha3/xkcp_sha3.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/xkcp_sha3x4.o: $(SRC)/common/sha3/xkcp_sha3x4.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/sha3.o: $(SRC)/common/sha3/sha3.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/sha3x4.o: $(SRC)/common/sha3/sha3x4.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# --- Intel AVX512VL Keccak (whole-sponge x1+x4) ---
$(BUILD)/common/sha3/avx512vl_sha3.o: $(SRC)/common/sha3/avx512vl_sha3.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/avx512vl_sha3x4.o: $(SRC)/common/sha3/avx512vl_sha3x4.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/avx512vl_low/%.o: $(SRC)/common/sha3/avx512vl_low/%.S | $(BUILD)/common/sha3/avx512vl_low
	$(CC) -Wa,--noexecstack -fPIC $(DEPFLAGS) -o $@ -c $<

# --- AVX2 Keccak (XKCP, vendored): permute (x1 .S + x4 SIMD256) + avx2-forced
#     xkcp backend with renamed callback structs ---
$(BUILD)/common/sha3/xkcp_low/KeccakP-1600/avx2/KeccakP-1600-AVX2.o: $(SRC)/common/sha3/xkcp_low/KeccakP-1600/avx2/KeccakP-1600-AVX2.S | $(BUILD)/common/sha3/xkcp_low/KeccakP-1600/avx2
	$(CC) -DADD_SYMBOL_SUFFIX $(INCS) -I./src/common/sha3/xkcp_low/KeccakP-1600/avx2 $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/avx2/KeccakP-1600-times4-SIMD256.o: $(SRC)/common/sha3/xkcp_low/KeccakP-1600times4/avx2/KeccakP-1600-times4-SIMD256.c | $(BUILD)/common/sha3/xkcp_low/KeccakP-1600times4/avx2
	$(CC) -DADD_SYMBOL_SUFFIX -mavx2 $(INCS) -I./src/common/sha3/xkcp_low/KeccakP-1600times4/avx2 -I./src/common/sha3/xkcp_low/KeccakP-1600/avx2 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/avx2_sha3.o: $(SRC)/common/sha3/avx2_sha3.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) -DOQS_ENABLE_SHA3_xkcp_low_avx2 -Dsha3_default_callbacks=sha3_avx2_callbacks -mavx2 $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/sha3/avx2_sha3x4.o: $(SRC)/common/sha3/avx2_sha3x4.c | $(BUILD)/common/sha3
	$(CC) $(OQS_DEFS) -DOQS_ENABLE_SHA3_xkcp_low_avx2 -Dsha3_x4_default_callbacks=sha3_x4_avx2_callbacks -mavx2 $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

$(BUILD)/common/%.o: $(SRC)/common/%.c | $(BUILD)/common
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# --- pqclean shims ---
$(BUILD)/common/pqclean_shims/%.o: $(SRC)/common/pqclean_shims/%.c | $(BUILD)/common/pqclean_shims
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# --- rand / rand_nist ---
$(BUILD)/common/rand/rand.o: $(SRC)/common/rand/rand.c | $(BUILD)/common/rand
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<
$(BUILD)/common/rand/rand_nist.o: $(SRC)/common/rand/rand_nist.c | $(BUILD)/common/rand
	$(CC) $(OQS_DEFS) $(INCS) $(CFLAGS_RANDNIST) $(DEPFLAGS) -o $@ -c $<

# --- AIMER ---
$(BUILD)/sig/aimer/sig_aimer_%.o: $(SRC)/sig/aimer/sig_aimer_%.c | $(BUILD)/sig/aimer
	$(CC) $(INCS) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# Per-impl SIMD flags + (for the optimized impls) the HASH_PREFIX_0 -D.
IMPLFLAGS_ref    :=
IMPLFLAGS_avx2   := $(AVX2_FLAGS)
IMPLFLAGS_avx512 := $(AVX512_FLAGS)

# $(1)=variant  $(2)=impl
define AIMER_IMPL_RULE
$(BUILD)/sig/aimer/$(1)_$(2)/%.o: $(SRC)/sig/aimer/aimer-$(1)_$(2)/%.c | $(BUILD)/sig/aimer/$(1)_$(2)
	$$(CC) $(OQS_DEFS) $(INCS) \
	      -I./$(SRC)/sig/aimer/aimer-$(1)_$(2) \
	      $(IMPLFLAGS_$(2)) $(if $(filter-out ref,$(2)),-DAIMER_HASH_PREFIX_0=$(PFX_$(1))) \
	      $$(CFLAGS_INTERNAL) $$(DEPFLAGS) -o $$@ -c $$<
endef
$(foreach v,$(AIMER_VARIANTS),$(foreach i,$(AIMER_IMPLS),$(eval $(call AIMER_IMPL_RULE,$(v),$(i)))))

# --- Top-level glue ---
$(BUILD)/sig/sig.o: $(SRC)/sig/sig.c | $(BUILD)/sig
	$(CC) $(INCS) -I./$(SRC) $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# --- Tests ---
$(BUILD)/tests/kat_sig.o: tests/kat_sig.c | $(BUILD)/tests
	$(CC) -DOQS_COMPILE_OPTIONS="\"[-Wa,--noexecstack;-O3;-fomit-frame-pointer;-fdata-sections;-ffunction-sections;-Wl,--gc-sections;-Wbad-function-cast]\"" \
	      $(INCS) -I./$(SRC) $(DEPFLAGS) -o $@ -c $<

$(BUILD)/tests/bench_sig.o: tests/bench_sig.c | $(BUILD)/tests
	$(CC) -DOQS_COMPILE_OPTIONS="\"[-O3;-fomit-frame-pointer]\"" \
	      $(INCS) -I./$(SRC) $(DEPFLAGS) -o $@ -c $<

$(BUILD)/tests/bench_full.o: bench/bench_full.c | $(BUILD)/tests
	$(CC) -DOQS_COMPILE_OPTIONS="\"[-O3;-fomit-frame-pointer]\"" \
	      $(INCS) -I./$(SRC) -I./tests -O3 $(DEPFLAGS) -o $@ -c $<

$(BUILD)/tests/bench_full_paper.o: bench/bench_full_paper.c | $(BUILD)/tests
	$(CC) -DOQS_COMPILE_OPTIONS="\"[-O3;-fomit-frame-pointer]\"" \
	      $(INCS) -I./$(SRC) -I./tests -O3 -Wall -Wextra -fomit-frame-pointer \
	      $(DEPFLAGS) -o $@ -c $<

$(BUILD)/tests/test_helpers.o: tests/test_helpers.c | $(BUILD)/tests
	$(CC) -DOQS_COMPILE_OPTIONS="\"[-Wa,--noexecstack;-O3;-fomit-frame-pointer;-fdata-sections;-ffunction-sections;-Wl,--gc-sections;-Wbad-function-cast]\"" \
	      $(INCS) -I./$(SRC) $(DEPFLAGS) -o $@ -c $<

# Measurement-only Keccak-backend selector (test harness, not the algorithm).
$(BUILD)/tests/aimer_keccak_select.o: tests/aimer_keccak_select.c | $(BUILD)/tests
	$(CC) $(INCS) -I./src/common/sha3 $(CFLAGS_INTERNAL) $(DEPFLAGS) -o $@ -c $<

# ==== Housekeeping ====
.PHONY: clean distclean
clean:
	@echo "Cleaning build artifacts..."
	@find $(BUILD) -name '*.d' -delete || true
	@find $(BUILD) -name '*.o' -delete || true

distclean:
	$(RM) $(BUILD)
	$(RM) $(OUT)

# Include auto-deps
-include $(shell [ -d $(BUILD) ] && find $(BUILD) -name '*.d' 2>/dev/null)
