/* ov1.cpp — overlay 1: linked with the firmware, into the shared window. */
extern "C" float app_gain(void);
#define OV1_TEXT __attribute__((section(".ov1.text"), noinline, used))
extern "C" OV1_TEXT float float_add(float a, float b) { return a + b; }
extern "C" OV1_TEXT float float_scale(float a, float b) { return a * b * app_gain(); }
