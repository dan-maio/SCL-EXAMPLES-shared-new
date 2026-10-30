/* floatlib.cpp — a tiny loadable library: hard-float math, and one import from main. */
extern "C" float app_gain(void);          // left undefined: the loader binds it to main

extern "C" float float_add(float a, float b) { return a + b; }
extern "C" float float_mul(float a, float b) { return a * b; }
extern "C" float float_scale(float a, float b) { return a * b * app_gain(); }
