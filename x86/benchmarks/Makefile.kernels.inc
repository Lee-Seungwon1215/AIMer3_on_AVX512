# SPDX-License-Identifier: MIT
# Kernel-only benchmark extension for Makefile.liboqs.multi.

KERNEL_BENCH_BIN := $(BUILD)/bench/bench_kernels
BENCH_WRAPPER_OBJS := \
  $(BUILD)/bench/wrappers/128f/ref.o $(BUILD)/bench/wrappers/128f/avx2.o $(BUILD)/bench/wrappers/128f/avx512.o \
  $(BUILD)/bench/wrappers/128s/ref.o $(BUILD)/bench/wrappers/128s/avx2.o $(BUILD)/bench/wrappers/128s/avx512.o \
  $(BUILD)/bench/wrappers/192f/ref.o $(BUILD)/bench/wrappers/192f/avx2.o $(BUILD)/bench/wrappers/192f/avx512.o \
  $(BUILD)/bench/wrappers/192s/ref.o $(BUILD)/bench/wrappers/192s/avx2.o $(BUILD)/bench/wrappers/192s/avx512.o \
  $(BUILD)/bench/wrappers/256f/ref.o $(BUILD)/bench/wrappers/256f/avx2.o $(BUILD)/bench/wrappers/256f/avx512.o \
  $(BUILD)/bench/wrappers/256s/ref.o $(BUILD)/bench/wrappers/256s/avx2.o $(BUILD)/bench/wrappers/256s/avx512.o

define BENCH_WRAPPER_RULE
$(BUILD)/bench/wrappers/$(1)/$(2).o: benchmarks/kernel_wrapper.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(3) -DPARAMS=$(1) $(4) $(CFLAGS) -MMD -MP \
	      -c $$< -o $$@
endef

$(eval $(call BENCH_WRAPPER_RULE,128f,ref,$(REF_128F_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,128f,avx2,$(AVX2_128F_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,128f,avx512,$(OPT_128F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,128s,ref,$(REF_128S_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,128s,avx2,$(AVX2_128S_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,128s,avx512,$(OPT_128S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,192f,ref,$(REF_192F_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,192f,avx2,$(AVX2_192F_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,192f,avx512,$(OPT_192F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,192s,ref,$(REF_192S_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,192s,avx2,$(AVX2_192S_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,192s,avx512,$(OPT_192S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,256f,ref,$(REF_256F_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,256f,avx2,$(AVX2_256F_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,256f,avx512,$(OPT_256F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,256s,ref,$(REF_256S_INCLUDES),))
$(eval $(call BENCH_WRAPPER_RULE,256s,avx2,$(AVX2_256S_INCLUDES),$(AVX2_FLAGS) -DBENCH_BATCH))
$(eval $(call BENCH_WRAPPER_RULE,256s,avx512,$(OPT_256S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))

$(KERNEL_BENCH_BIN): benchmarks/bench_kernels.c $(BENCH_WRAPPER_OBJS) $(OQS_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(LDFLAGS) \
	      $< $(BENCH_WRAPPER_OBJS) $(OQS_LIB) $(LDLIBS) -lm -o $@

oqs-bench: $(KERNEL_BENCH_BIN)

-include $(BENCH_WRAPPER_OBJS:.o=.d)
