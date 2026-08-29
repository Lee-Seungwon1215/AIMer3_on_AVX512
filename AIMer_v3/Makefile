# SPDX-License-Identifier: MIT

CC       ?= cc
CPPFLAGS ?=
CFLAGS   ?= -O3 -Wall -Wextra -fomit-frame-pointer -std=c11
LDFLAGS  ?=
LDLIBS   ?=

VARIANTS := 128f 128s 192f 192s 256f 256s
IMPLS    := ref opt

BUILD      := build
SRC_ROOT   := src
AIMER_ROOT := $(SRC_ROOT)/sig/aimer

PARAM ?= 128f
IMPL  ?= ref

COMMON_NAMES := aes.c fips202.c rng.c
COMMON_OBJS  := $(addprefix $(BUILD)/common/,$(COMMON_NAMES:.c=.o))
CORE_NAMES   := aim3.c field_common.c hash.c sign.c tree.c

field_bits = $(patsubst %f,%,$(patsubst %s,%,$(1)))
impl_dir   = $(AIMER_ROOT)/aimer-$(1)_$(2)
impl_build = $(BUILD)/aimer-$(1)/$(2)
impl_core_objects = $(addprefix $(call impl_build,$(1),$(2))/,$(CORE_NAMES:.c=.o)) \
                    $(call impl_build,$(1),$(2))/field$(call field_bits,$(1)).o
impl_bins = $(call impl_build,$(1),$(2))/test_sign \
            $(call impl_build,$(1),$(2))/PQCgenKAT_sign

REF_BINS := $(foreach variant,$(VARIANTS),$(call impl_bins,$(variant),ref))
OPT_BINS := $(foreach variant,$(VARIANTS),$(call impl_bins,$(variant),opt))
ALL_BINS := $(REF_BINS) $(OPT_BINS)

.PHONY: all ref opt test test-ref test-opt kat kat-ref-all kat-opt-all check clean help

all: ref opt

ref: $(REF_BINS)

opt: $(OPT_BINS)

$(BUILD)/common/%.o: $(SRC_ROOT)/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -I$(SRC_ROOT) $(CFLAGS) -MMD -MP -c $< -o $@

define implementation_template
IMPL_DIR_$(1)_$(2) := $(call impl_dir,$(1),$(2))
IMPL_OBJS_$(1)_$(2) := $(call impl_core_objects,$(1),$(2))

$(call impl_build,$(1),$(2))/%.o: $$(IMPL_DIR_$(1)_$(2))/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) -I$$(IMPL_DIR_$(1)_$(2)) -I$$(SRC_ROOT) \
		-DPARAMS=$(1) $$(CFLAGS) -MMD -MP -c $$< -o $$@

$(call impl_build,$(1),$(2))/test_sign.o: tests/test_sign.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) -I$$(IMPL_DIR_$(1)_$(2)) -I$$(SRC_ROOT) \
		-DPARAMS=$(1) $$(CFLAGS) -MMD -MP -c $$< -o $$@

$(call impl_build,$(1),$(2))/PQCgenKAT_sign.o: tests/PQCgenKAT_sign.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) -I$$(IMPL_DIR_$(1)_$(2)) -I$$(SRC_ROOT) \
		-DPARAMS=$(1) $$(CFLAGS) -MMD -MP -c $$< -o $$@

$(call impl_build,$(1),$(2))/test_sign: \
	$(call impl_build,$(1),$(2))/test_sign.o \
	$$(IMPL_OBJS_$(1)_$(2)) $$(COMMON_OBJS)
	$$(CC) $$(LDFLAGS) $$^ $$(LDLIBS) -o $$@

$(call impl_build,$(1),$(2))/PQCgenKAT_sign: \
	$(call impl_build,$(1),$(2))/PQCgenKAT_sign.o \
	$$(IMPL_OBJS_$(1)_$(2)) $$(COMMON_OBJS)
	$$(CC) $$(LDFLAGS) $$^ $$(LDLIBS) -o $$@
endef

$(foreach variant,$(VARIANTS), \
  $(foreach impl,$(IMPLS), \
    $(eval $(call implementation_template,$(variant),$(impl)))))

test-ref: ref
	@set -e; \
	for variant in $(VARIANTS); do \
		$(BUILD)/aimer-$$variant/ref/test_sign >/dev/null; \
		echo "[PASS] aimer-$$variant ref smoke test"; \
	done

test-opt: opt
	@set -e; \
	for variant in $(VARIANTS); do \
		$(BUILD)/aimer-$$variant/opt/test_sign >/dev/null; \
		echo "[PASS] aimer-$$variant opt smoke test"; \
	done

test: test-ref test-opt

SELECTED_KAT_DIR := $(BUILD)/aimer-$(PARAM)/$(IMPL)
SELECTED_KAT_BIN := $(SELECTED_KAT_DIR)/PQCgenKAT_sign

kat: $(SELECTED_KAT_BIN)
	@if ! echo " $(VARIANTS) " | grep -q " $(PARAM) "; then \
		echo "Unknown PARAM=$(PARAM). Choose one of: $(VARIANTS)"; exit 2; \
	fi
	@if ! echo " $(IMPLS) " | grep -q " $(IMPL) "; then \
		echo "Unknown IMPL=$(IMPL). Choose one of: $(IMPLS)"; exit 2; \
	fi
	@cd $(SELECTED_KAT_DIR) && ./PQCgenKAT_sign >/dev/null
	@expected=$$(find KAT/aimer-$(PARAM) -name 'PQCsignKAT_*.rsp' -print -quit); \
	generated=$$(find $(SELECTED_KAT_DIR) -name 'PQCsignKAT_*.rsp' -print -quit); \
	if cmp -s "$$expected" "$$generated"; then \
		echo "[PASS] aimer-$(PARAM) $(IMPL) official KAT"; \
	else \
		echo "[FAIL] aimer-$(PARAM) $(IMPL) official KAT"; exit 1; \
	fi

kat-ref-all: ref
	@set -e; \
	for variant in $(VARIANTS); do \
		$(MAKE) --no-print-directory kat PARAM=$$variant IMPL=ref; \
	done

kat-opt-all: opt
	@set -e; \
	for variant in $(VARIANTS); do \
		$(MAKE) --no-print-directory kat PARAM=$$variant IMPL=opt; \
	done

check: test kat-ref-all kat-opt-all

clean:
	rm -rf $(BUILD)

help:
	@echo "make all                         Build ref and opt for all parameter sets"
	@echo "make test                        Run ref and opt sign/verify smoke tests"
	@echo "make kat PARAM=128f IMPL=ref     Check one implementation against official KAT"
	@echo "make kat-ref-all                 Check every ref implementation against KAT"
	@echo "make kat-opt-all                 Check every opt implementation against KAT"
	@echo "make check                       Run all smoke tests and all KAT checks"

DEPS := $(COMMON_OBJS:.o=.d) \
	$(foreach variant,$(VARIANTS), \
	  $(foreach impl,$(IMPLS), \
	    $(patsubst %.o,%.d,$(call impl_core_objects,$(variant),$(impl))) \
	    $(call impl_build,$(variant),$(impl))/test_sign.d \
	    $(call impl_build,$(variant),$(impl))/PQCgenKAT_sign.d))
-include $(DEPS)
