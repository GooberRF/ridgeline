#pragma once
#include <common/version/version.h>

// Minidump information level: 0 = smallest (stacks + module list),
// 2 = biggest (full memory). Keep at 0 for shipping builds; the resulting
// dump is small enough to attach to issue reports.
#ifdef NDEBUG
#define CRASHHANDLER_DMP_LEVEL 0
#else
#define CRASHHANDLER_DMP_LEVEL 0
#endif
