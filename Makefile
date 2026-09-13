# SPDX-License-Identifier: MIT

.DEFAULT_GOAL := x86

.PHONY: all x86 x86-test x86-kat x86-check x86-bench \
	m55-host-check help

all: x86

x86:
	$(MAKE) -C x86

x86-test:
	$(MAKE) -C x86 oqs-test

x86-kat:
	$(MAKE) -C x86 oqs-kat

x86-check:
	$(MAKE) -C x86 check

x86-bench:
	$(MAKE) -C x86 bench

m55-host-check:
	@for p in 128f 128s 192f 192s 256f 256s; do \
		$(MAKE) -C m55 PARAM=$$p BACKEND=mve MATVEC=mve check-mve-field-host check-kat-mve-host || exit $$?; \
	done

help:
	@echo "x86, x86-test, x86-kat, x86-check, x86-bench, m55-host-check"
