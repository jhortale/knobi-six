/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: the factory programs (store_prog.h prog_factory, prog_factory_count), after factory.h. Moved out of
 * store_prog.c unchanged (included there at the same place: the firmware build is the same) so the host core
 * (firmware/src/core/knobi_core.c) shares them. */
uint32_t prog_factory_count(void) { return FACTORY_COUNT; }

int prog_factory(uint32_t n, program_t *p)
{
#if FACTORY_COUNT
    if (n < FACTORY_COUNT && program_packed_size(factory_bank[n][0]) == FACTORY_SIZE &&
        program_unpack(p, factory_bank[n], FACTORY_SIZE) == 0) {
        program_set_seq_gen(p, 0);                     /* (a factory program names no stored sequence: its own is the
                                                        * bank's, prog_seq.c factory_seq) */
        return 0;
    }
#else
    (void)n;
#endif
    program_init(p);
    return -1;
}
