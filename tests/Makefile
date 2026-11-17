# SPDX-License-Identifier: GPL-3.0-only
# Unit tests: every tests/unit/test_*.c is built for the host and run; any failure makes the target fail.
# KNOBI_NO_FACTORY: no factory bank (a local firmware/gen/factory_bank.h does not change the results); test_factory*.c
# pick their own.
#   make -C tests unit
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Werror -DDINGHY_HOST -DKNOBI_NO_FACTORY -I../firmware/src -I../firmware/hal -I../firmware/gen
TESTS := $(basename $(notdir $(wildcard unit/test_*.c)))
OUT = ../build/unit

.PHONY: unit
unit:
	@mkdir -p $(OUT)
	@set -e; for t in $(TESTS); do \
	  echo "== $$t"; $(CC) $(CFLAGS) -o $(OUT)/$$t unit/$$t.c -lm; $(OUT)/$$t; \
	done
