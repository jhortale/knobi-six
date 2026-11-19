# SPDX-License-Identifier: GPL-3.0-only
# Unit tests: every tests/unit/test_*.c is built for the host and run; any failure makes the target fail.
# KNOBI_NO_FACTORY: no factory bank (a local firmware/gen/factory_bank.h does not change the results); test_factory*.c
# pick their own.
#   make -C tests unit
# test_core.c (the host core, firmware/src/core/) compares with the firmware's host build: unit first builds tests/host.c
# with firmware/src/app.c as tests/run_host.sh does and plays tests/scripts/core_ref.txt into $(OUT)/core_ref.wav. Both
# with FP_EXACT: no fused multiply-add (the filter's float path; a compiler fuses differently in different files).
#   make -C tests tsan    test_split.c's split_threads_tsan: set B on a second thread, under ThreadSanitizer (skipped,
#                         not failed, where a -fsanitize=thread program cannot be built or run)
CC ?= cc
CFLAGS = -std=c11 -O2 -Wall -Werror -DDINGHY_HOST -DKNOBI_NO_FACTORY -I../firmware/src -I../firmware/hal -I../firmware/gen
TESTS := $(basename $(notdir $(wildcard unit/test_*.c)))
OUT = ../build/unit
FP_EXACT = -ffp-contract=off

.PHONY: unit tsan core-ref
unit: core-ref
	@mkdir -p $(OUT)
	@set -e; for t in $(TESTS); do \
	  case $$t in test_core) x="$(FP_EXACT)";; *) x=;; esac; \
	  echo "== $$t"; $(CC) $(CFLAGS) $$x -o $(OUT)/$$t unit/$$t.c -lm; $(OUT)/$$t; \
	done

core-ref:
	@mkdir -p $(OUT)
	@echo "== core_ref (the firmware's host build plays scripts/core_ref.txt: test_core's reference)"
	@$(CC) -O2 $(FP_EXACT) -Wall -Wno-unused-function -Wno-unused-variable -DDINGHY_HOST -DBOARD_SMK37=1 -DKNOBI_NO_FACTORY -I../firmware/hal -I../firmware/src \
	  -I../firmware/gen -o $(OUT)/core_ref_app ../firmware/src/app.c -lm
	@$(OUT)/core_ref_app $(OUT)/core_ref scripts/core_ref.txt > $(OUT)/core_ref.log || { cat $(OUT)/core_ref.log; exit 1; }

tsan:
	@mkdir -p $(OUT)
	@echo "== test_split (threads, -fsanitize=thread)"
	@if echo 'int main(void){return 0;}' | $(CC) -fsanitize=thread -x c -o $(OUT)/tsan_probe - >/dev/null 2>&1 && \
	    $(OUT)/tsan_probe >/dev/null 2>&1; then \
	  $(CC) $(CFLAGS) -O1 -g -fsanitize=thread -pthread -DSPLIT_THREADS -o $(OUT)/test_split_tsan unit/test_split.c -lm && \
	  TSAN_OPTIONS="halt_on_error=1 $$TSAN_OPTIONS" $(OUT)/test_split_tsan; \
	else echo "tsan: $(CC) cannot build or run a -fsanitize=thread program here: skipped"; fi
