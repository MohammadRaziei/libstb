// utest.h pulls in <windows.h> on Windows, which #defines min, max, small,
// near, far ... (`small` is `char`). Keep that header lean and macro-free.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "utest.h"

UTEST_MAIN();