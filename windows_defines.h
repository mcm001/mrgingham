#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define WPI_EXPORT __declspec(dllexport)
#define WPI_RESTRICT __restrict
#undef WPI_LINUX
#define WPI_WIN
#else
#define WPI_EXPORT __attribute__((visibility("default")))
#define WPI_RESTRICT __restrict__
#undef WPI_WIN
#define WPI_LINUX
#endif