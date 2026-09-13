# SPDX-License-Identifier: MIT
# Diagnostic-only AVX-512 causal decomposition. The official paper benchmark
# targets and their kernel list are deliberately left unchanged.

CAUSE_BENCH_BIN := $(BUILD)/bench/bench_avx512_causes
CAUSE_WRAPPER_OBJS := \
  $(BUILD)/bench/cause-wrappers/128f/ref.o $(BUILD)/bench/cause-wrappers/128f/avx512.o \
  $(BUILD)/bench/cause-wrappers/128s/ref.o $(BUILD)/bench/cause-wrappers/128s/avx512.o \
  $(BUILD)/bench/cause-wrappers/192f/ref.o $(BUILD)/bench/cause-wrappers/192f/avx512.o \
  $(BUILD)/bench/cause-wrappers/192s/ref.o $(BUILD)/bench/cause-wrappers/192s/avx512.o \
  $(BUILD)/bench/cause-wrappers/256f/ref.o $(BUILD)/bench/cause-wrappers/256f/avx512.o \
  $(BUILD)/bench/cause-wrappers/256s/ref.o $(BUILD)/bench/cause-wrappers/256s/avx512.o

define CAUSE_WRAPPER_RULE
$(BUILD)/bench/cause-wrappers/$(1)/$(2).o: benchmarks/cause_wrapper.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(3) -DPARAMS=$(1) $(4) $(CFLAGS) -MMD -MP \
	      -c $$< -o $$@
endef

$(eval $(call CAUSE_WRAPPER_RULE,128f,ref,$(REF_128F_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,128f,avx512,$(OPT_128F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call CAUSE_WRAPPER_RULE,128s,ref,$(REF_128S_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,128s,avx512,$(OPT_128S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call CAUSE_WRAPPER_RULE,192f,ref,$(REF_192F_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,192f,avx512,$(OPT_192F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call CAUSE_WRAPPER_RULE,192s,ref,$(REF_192S_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,192s,avx512,$(OPT_192S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call CAUSE_WRAPPER_RULE,256f,ref,$(REF_256F_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,256f,avx512,$(OPT_256F_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))
$(eval $(call CAUSE_WRAPPER_RULE,256s,ref,$(REF_256S_INCLUDES),))
$(eval $(call CAUSE_WRAPPER_RULE,256s,avx512,$(OPT_256S_INCLUDES),$(AVX512_FLAGS) -DBENCH_BATCH))

$(CAUSE_BENCH_BIN): benchmarks/bench_avx512_causes.c $(CAUSE_WRAPPER_OBJS) $(OQS_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(LDFLAGS) \
	      $< $(CAUSE_WRAPPER_OBJS) $(OQS_LIB) $(LDLIBS) -lm -o $@

.PHONY: oqs-cause-bench
oqs-cause-bench: $(CAUSE_BENCH_BIN) $(BENCH_BIN)

CAUSE_DEPS := $(CAUSE_WRAPPER_OBJS:.o=.d)
-include $(CAUSE_DEPS)
