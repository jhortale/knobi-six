# SPDX-License-Identifier: GPL-3.0-only
# Unit tests: every tests/unit/test_*.c is built for the host and run; any failure makes the target fail.
#   make -C tests unit
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Werror -DDINGHY_HOST -I../firmware/src -I../firmware/hal -I../firmware/gen
TESTS := $(basename $(notdir $(wildcard unit/test_*.c)))
OUT = ../build/unit

.PHONY: unit
unit:
	@mkdir -p $(OUT)
	@set -e; for t in $(TESTS); do \
	  echo "== $$t"; $(CC) $(CFLAGS) -o $(OUT)/$$t unit/$$t.c; $(OUT)/$$t; \
	done
