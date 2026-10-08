/*
 * fn.h
 *
 * Closures as one pointer. Fn(A1, ..., An, R) is the type
 *
 *   R (**)(void *env, A1, ..., An)
 *
 * a pointer to the code pointer at the start of a closure record,
 *
 *   struct { R (*code)(void *env, A1, ..., An); void *env; }
 *
 * and apply() follows it to the record and calls code with env. Fn types are
 * plain C pointer types, so they need no declaring: Fn(Dbl, Dbl) is the same
 * type wherever it is written, and its arguments can be any type, including
 * multi-token ones like `const char *`.
 *
 *   typedef struct { Dbl w1, w2; } Weighted;
 *   static Dbl weighted(void *env, Dbl x, Dbl y) {
 *       const Weighted *self = env;
 *       return self->w1 * x + self->w2 * y;
 *   }
 *
 *   Weighted w = { 0.25, 0.75 };
 *   Fn(Dbl, Dbl, Dbl) f = FN(weighted, &w);
 *   apply(f, 4, 8);
 *
 * Closure code takes its environment as a `void *`. A method such as
 * area(Circle), taking a `const Circle *`, becomes a closure through a
 * one-line adapter. Calling it through a `void *` signature directly would
 * be undefined; FN(area(Circle), &c) is not a Fn(Dbl), which Clang and GCC
 * diagnose but MSVC accepts silently.
 *
 *   static Dbl circle_area(void *env) { return area(Circle)(env); }
 *
 * A Fn borrows its record, and the record borrows env. FN's record is a
 * compound literal: it lives until the end of the enclosing block, and an FN
 * evaluated repeatedly, as in a loop, reuses the same record. For a closure
 * that must outlive the block, or one per iteration, declare a Closure(...)
 * where it should live and use &record.code as the Fn.
 *
 * Requires typeof, since `R (**)(void *) f;` is not a declaration: C23, or
 * __typeof__ in earlier modes (GCC, Clang, MSVC 17.9+).
 */
#ifndef FN_H
#define FN_H

#include <stddef.h>            /* offsetof */
#include "dyn.h"               /* helpers */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  define FN_TYPEOF_(T)  typeof(T)
#else
#  define FN_TYPEOF_(T)  __typeof__(T)
#endif


/* -------------------------------------------------------------------------- */
/* Closure types                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Fn(A1, ..., An, R)        the closure type, n = 0..7, result last
 * Closure(A1, ..., An, R)   a record a Fn of that type can point to:
 *
 *   struct {
 *       R (*code)(void *env, A1, ..., An);
 *       void *env;
 *   };
 *
 * Each Closure(...) is a new anonymous struct type, so use it to declare
 * variables, members and arrays, not parameters; pass the Fn instead.
 */
#define Fn(...)       FN_EXPAND_(FN_POINTER_, (FN_SIG_(__VA_ARGS__)))
#define Closure(...)  FN_EXPAND_(FN_CLOSURE_, (FN_SIG_(__VA_ARGS__)))

#define FN_POINTER_(R, PARAMS)  FN_TYPEOF_(R (**) PARAMS)
#define FN_CLOSURE_(R, PARAMS)  FN_RECORD_(R (*) PARAMS)

/* (A1, ..., An, R)  ->  R, (void *, A1, ..., An) */
#define FN_SIG_(...)                                                          \
    MONOMORPH_CAT(FN_SIG_, MONOMORPH_NARG(__VA_ARGS__))(__VA_ARGS__)
#define FN_SIG_1(r)                       r, (void *)
#define FN_SIG_2(a, r)                    r, (void *, a)
#define FN_SIG_3(a, b, r)                 r, (void *, a, b)
#define FN_SIG_4(a, b, c, r)              r, (void *, a, b, c)
#define FN_SIG_5(a, b, c, d, r)           r, (void *, a, b, c, d)
#define FN_SIG_6(a, b, c, d, e, r)        r, (void *, a, b, c, d, e)
#define FN_SIG_7(a, b, c, d, e, f, r)     r, (void *, a, b, c, d, e, f)
#define FN_SIG_8(a, b, c, d, e, f, g, r)  r, (void *, a, b, c, d, e, f, g)
#define FN_SIG_MONOMORPH_TOO_MANY(...)                                        \
    FN_ERROR_at_most_7_parameters, (void *)

#define FN_EXPAND_(m, args)  m args

/*
 * A record whose code has function pointer type F. apply() reads env at its
 * offset in struct fn_record_, which is the same for every F as long as all
 * function pointers have one size.
 */
#define FN_RECORD_(F)                                                         \
    struct {                                                                  \
        FN_TYPEOF_(F) code;                                                   \
        void *env;                                                            \
        _Static_assert(sizeof(F) == sizeof(void (*)(void)),                   \
                       "fn.h: function pointer types differ in size");        \
    }

struct fn_record_ { void (*code)(void); void *env; };


/* -------------------------------------------------------------------------- */
/* Making closures                                                            */
/* -------------------------------------------------------------------------- */

/*
 * FN(code, env)   the closure of code over env: a Fn pointing to a new
 *                 record { code, env }.
 *
 *   code is a function or function pointer taking `void *env` first; env is
 *   any object pointer. The Fn's type comes from code's, and is checked
 *   where the result meets the Fn type it is assigned or passed to. A
 *   mismatch is an incompatible-pointer-types diagnostic: an error in GCC 14
 *   and later, a warning in Clang and MSVC, so build with
 *   -Werror=incompatible-pointer-types or /we4113.
 */
#define FN(code_, env_)                                                       \
    (FN_QUIET_ &(FN_RECORD_(FN_TYPEOF_(&*(code_)))){ (code_), (env_) }.code)

/* MSVC warns about the record's struct type being defined in parentheses. */
#if defined(_MSC_VER) && !defined(__clang__)
#  define FN_QUIET_  __pragma(warning(suppress: 4116))
#else
#  define FN_QUIET_
#endif


/* -------------------------------------------------------------------------- */
/* Calling                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * apply(f, args...)   calls closure f, a Fn, with up to 7 arguments.
 * apply(f)            calls a closure that takes no arguments.
 * f is evaluated twice, so pass a variable.
 */
#define apply(...)                                                            \
    MONOMORPH_CAT(FN_APPLY_, DYN_HAS_PARAMS(~, __VA_ARGS__))(__VA_ARGS__)
#define FN_APPLY_0(f)       ((*(f))(FN_ENV_(f)))
#define FN_APPLY_1(f, ...)  ((*(f))(FN_ENV_(f), __VA_ARGS__))

/*
 * The env of the record f points into. It is read through a `void *` lvalue,
 * the type it was stored as, rather than as a member of struct fn_record_:
 * that is not the record's type, and GCC's and Clang's type-based alias
 * analysis may assume members of unrelated struct types never overlap.
 */
#define FN_ENV_(f)                                                            \
    (*(void *const *)((const char *)(f) + offsetof(struct fn_record_, env)))

#endif /* FN_H */
