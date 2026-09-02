# SPDX-License-Identifier: MIT
# Canonical AIMer v3 build: reference, AVX2, and AVX-512 OQS backends.

.DEFAULT_GOAL := all

include Makefile.liboqs.multi

PARAM ?= 128f
VALID_PARAMS := 128f 128s 192f 192s 256f 256s
BENCH_ITERS ?= 50
BENCH_WARMUP ?=
BENCH_CORE ?=
BENCH_RESULT_DIR ?=
BENCH_VERIFY_KAT ?= 1

.PHONY: test kat check check-all bench bench-run help

test: oqs-test

kat:
	@if ! echo " $(VALID_PARAMS) " | grep -q " $(PARAM) "; then \
		echo "Unknown PARAM=$(PARAM). Choose one of: $(VALID_PARAMS)"; exit 2; \
	fi
	$(MAKE) -f Makefile.liboqs.multi oqs-kat-$(PARAM)

check: oqs-check

check-all: oqs-check

bench: oqs-bench

bench-run: oqs-bench
	CORE="$(BENCH_CORE)" WARMUP="$(BENCH_WARMUP)" \
	RESULT_DIR="$(BENCH_RESULT_DIR)" VERIFY_KAT="$(BENCH_VERIFY_KAT)" \
		./benchmarks/run_all.sh "$(BENCH_ITERS)"

help:
	@echo "make                 Build the self-contained OQS library and tests"
	@echo "make oqs-kat         Run official KATs for all six sets and three backends"
	@echo "make kat PARAM=128f  Run one parameter set across all three backends"
	@echo "make oqs-test        Run GF and OQS registry/sign/verify tests"
	@echo "make check           Run all KAT, GF, and OQS correctness tests"
	@echo "make bench           Build the three-backend benchmark"
	@echo "make bench-run       Verify KATs, benchmark all sets, and compare results"
	@echo "make bench-paper     Rebuild O3, validate, and run repeated end-to-end/kernel measurements"
	@echo "make bench-kernel-paper  Calibrate and run 15 paper-grade optimized-kernel measurements"
	@echo "make bench-kernel-paper-isolated  Run final kernel benchmark on an exclusive physical core"
include benchmarks/Makefile.paper.inc
