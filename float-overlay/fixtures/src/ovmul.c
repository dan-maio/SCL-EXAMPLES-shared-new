/* ovmul.c — overlay module 2. float_mul, bound to the SAME window VMA as ovadd
 * but stored at its own flash LMA. Only one of {ovadd, ovmul} is resident at a
 * time; main loads the right one before calling. */
#define OVMUL __attribute__((section(".ovmul.text"), noinline, used))
OVMUL float ov_mul(float a, float b) { return a * b; }
