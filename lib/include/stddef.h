#if !defined __need_size_t && !defined __need_ptrdiff_t && !defined __need_wchar_t && \
    !defined __need_wint_t && !defined __need_NULL
#define __need_size_t
#define __need_ptrdiff_t
#define __need_wchar_t
#define __need_NULL
#define __need_offsetof
#endif

#if defined __need_size_t && !defined _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef __SIZE_TYPE__ size_t;
#endif

#if defined __need_ptrdiff_t && !defined _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif

#if defined __need_wchar_t && !defined _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
typedef __WCHAR_TYPE__ wchar_t;
#endif

#if defined __need_wint_t && !defined _WINT_T
#define _WINT_T
typedef __WINT_TYPE__ wint_t;
#endif

#ifdef __need_NULL
#undef NULL
#define NULL ((void *)0)
#endif

#if defined __need_offsetof && !defined offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#undef __need_size_t
#undef __need_ptrdiff_t
#undef __need_wchar_t
#undef __need_wint_t
#undef __need_NULL
#undef __need_offsetof
