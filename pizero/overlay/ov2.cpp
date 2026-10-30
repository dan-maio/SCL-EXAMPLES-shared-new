/* ov2.cpp — overlay 2: shares the window with overlay 1, so only one is resident. */
#define OV2_TEXT __attribute__((section(".ov2.text"), noinline, used))
extern "C" OV2_TEXT float float_mul(float a, float b) { return a * b; }
