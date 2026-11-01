/* ovfloat.cpp — the float overlay module (float_add / float_mul). Time-shares the
 * SRAM window with the spifs driver: only one is resident at a time. NON-PIC +
 * -mlong-calls, gathered by the linker into .ovfloat. */
extern "C" float float_add(float a, float b) { return a + b; }
extern "C" float float_mul(float a, float b) { return a * b; }
