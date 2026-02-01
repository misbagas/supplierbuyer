#pragma once

#include <cstring>
#include <cstdio>
#include <cstdint>

#ifdef _WIN32
  #include <windows.h>

  #define STRNCPY(dest, size, src) strncpy_s(dest, size, src, _TRUNCATE)
  #define SPRINTF(dest, size, fmt, ...) sprintf_s(dest, size, fmt, __VA_ARGS__)
  #define SLEEP_MS(ms) Sleep(ms)
  #define GET_TICK_MS() GetTickCount()

#else
  #include <unistd.h>
  #include <time.h>

  #define STRNCPY(dest, size, src)            \
    do {                                     \
      strncpy(dest, src, size - 1);          \
      dest[size - 1] = '\0';                 \
    } while (0)

  #define SPRINTF(dest, size, fmt, ...)       \
    snprintf(dest, size, fmt, __VA_ARGS__)

  #define SLEEP_MS(ms) usleep((ms) * 1000)

  inline uint64_t GET_TICK_MS() {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
  }
#endif
