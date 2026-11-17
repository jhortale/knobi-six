# SPDX-License-Identifier: GPL-3.0-only
# Unit tests: every tests/unit/test_*.c is built for the host and run; any failure makes the target fail.
# KNOBI_NO_FACTORY: no factory bank (a local firmware/gen/factory_bank.h does not change the results); test_factory*.c
# pick their own.
#   make -C tests unit
#   make -C tests tsan    test_split.c's split_threads_tsan: set B on a second thread, under ThreadSanitizer (skipped,
#                         not failed, where the compiler cannot build with -fsanitize=thread)
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Werror -DDINGHY_HOST -DKNOBI_NO_FACTORY -I../firmware/src -I../firmware/hal -I../firmware/gen
TESTS := $(basename $(notdir $(wildcard unit/test_*.c)))
OUT = ../build/unit

.PHONY: unit tsan
unit:
	@mkdir -p $(OUT)
	@set -e; for t in $(TESTS); do \
	  echo "== $$t"; $(CC) $(CFLAGS) -o $(OUT)/$$t unit/$$t.c -lm; $(OUT)/$$t; \
	done

tsan:
	@mkdir -p $(OUT)
	@echo "== test_split (threads, -fsanitize=thread)"
	@if echo 'int main(void){return 0;}' | $(CC) -fsanitize=thread -x c -o $(OUT)/tsan_probe - >/dev/null 2>&1; then \
	  $(CC) $(CFLAGS) -O1 -g -fsanitize=thread -pthread -DSPLIT_THREADS -o $(OUT)/test_split_tsan unit/test_split.c -lm && \
	  TSAN_OPTIONS="halt_on_error=1 $$TSAN_OPTIONS" $(OUT)/test_split_tsan; \
	else echo "tsan: $(CC) cannot build with -fsanitize=thread here: skipped"; fi
