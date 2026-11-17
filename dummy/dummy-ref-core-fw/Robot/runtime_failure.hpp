#pragma once

#include <cstdlib>

#if defined(__arm__) || defined(__thumb__)
extern "C" void Error_Handler(void);
#endif

[[noreturn]] inline void HandleRuntimeInitFailure()
{
#if defined(__arm__) || defined(__thumb__)
    Error_Handler();
    for (;;) {}
#else
    std::abort();
#endif
}
