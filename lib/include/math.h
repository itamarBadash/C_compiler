#include_next <math.h>

#if defined _WIN32 && !defined math_errhandling
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERRNO
#endif
