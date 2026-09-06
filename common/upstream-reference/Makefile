# SPDX-License-Identifier: MIT
PARAMS_LIST = 128f 128s 192f 192s 256f 256s

ifneq ($(SUBMAKE),1)
.PHONY: all $(PARAMS_LIST) clean check
all: $(PARAMS_LIST)

$(PARAMS_LIST):
	@$(MAKE) SUBMAKE=1 PARAMS=$@ OBJDIR=obj/$@ BUILD_DIR=build/$@ all_inner

check: all
	@for p in $(PARAMS_LIST); do \
	  ( cd build/$$p && ./PQCgenKAT_sign > /dev/null ) || exit 1; \
	  if diff -q ../KAT/aimer-$$p/PQCsignKAT_*.rsp build/$$p/PQCsignKAT_*.rsp > /dev/null; then \
	    echo "=== KAT check aimer-$$p - PASS ==="; \
	  else \
	    echo "=== KAT check aimer-$$p - FAIL ==="; exit 1; \
	  fi; \
	done
	@echo "All KAT checks passed."

clean:
	@echo "=== Removing obj/ and build/ directories ==="
	@rm -rf obj build
	@rm -f PQCsignKAT_*.rsp PQCsignKAT_*.req
	@echo "Clean complete."
else
CC      ?= gcc
CFLAGS  += -I. -O3 -Wall -Wextra -fomit-frame-pointer -DPARAMS=$(PARAMS)

BASE_TESTSIGN = test_sign
BASE_KAT      = PQCgenKAT_sign

EXE_TESTSIGN = $(BUILD_DIR)/$(BASE_TESTSIGN)
EXE_KAT      = $(BUILD_DIR)/$(BASE_KAT)
ALL_EXES     = $(EXE_TESTSIGN) $(EXE_KAT)

COMMON_SRC = common/aes.c common/fips202.c common/rng.c
HEADERS    = aim3.h api.h field.h field_internal.h hash.h params.h sign.h tree.h
SOURCES    = aim3.c field_common.c hash.c sign.c tree.c
ifneq (,$(findstring 128,$(PARAMS)))
    SOURCES += field128.c
endif
ifneq (,$(findstring 192,$(PARAMS)))
    SOURCES += field192.c
endif
ifneq (,$(findstring 256,$(PARAMS)))
    SOURCES += field256.c
endif

SRC_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES) $(COMMON_SRC))

.PHONY: all_inner
all_inner: $(ALL_EXES)

$(OBJDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/test_%.o: test/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

$(EXE_TESTSIGN): $(OBJDIR)/test_$(BASE_TESTSIGN).o $(SRC_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(EXE_KAT): $(OBJDIR)/test_$(BASE_KAT).o $(SRC_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean_inner
clean_inner:
	@echo "Cleaning build for $(PARAMS) ..."
	@rm -rf $(OBJDIR) $(BUILD_DIR)
	@echo "Clean for $(PARAMS) done."
endif
