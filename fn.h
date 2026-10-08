/*
 * fn.h
 *
 * Closures as two-pointer values, an environment pointer plus a code
 * pointer: the one-method case of dyn.h's interfaces. An environment is any
 * struct, and a closure's code is any method of it, i.e. any overload
 * OVERLOAD(name, T) taking a `const T *` or `T *` first, so every method can
 * also be used as a closure.
 *
 *   FN_DEFINE(Dbl, Dbl, Dbl);                     the type Fn(Dbl, Dbl, Dbl)
 *
 *   #define apply(T)  OVERLOAD(apply, T)
 *
 *   typedef struct { Dbl w1, w2; } Weighted;
 *   Dbl apply(Weighted)(const Weighted *self, Dbl x, Dbl y) {
 *       return self->w1 * x + self->w2 * y;
 *   }
 *   BIND(Fn(Dbl, Dbl, Dbl), Weighted, apply);     makes apply bindable
 *
 *   Weighted w = { 0.25, 0.75 };
 *   Fn(Dbl, Dbl, Dbl) f = FN(Weighted, apply, &w);
 *   CALL(f, 4, 8);
 *
 * A Fn borrows its environment: it must not outlive the object it was made
 * from. To return a closure, return the environment struct and FN it where
 * it is used.
 */
#ifndef FN_H
#define FN_H

#include "dyn.h"               /* helpers */

#define Fn(...)  TYPE(Fn, __VA_ARGS__)


/* -------------------------------------------------------------------------- */
/* Closure types                                                              */
/* -------------------------------------------------------------------------- */

/*
 * FN_DEFINE(A1, ..., An, R);   defines Fn(A1, ..., An, R), n = 0..7:
 *
 *   struct Fn(A1, ..., An, R) {
 *       void *self;                               the environment
 *       R (*code)(void *self, A1, ..., An);       what to call with it
 *   };
 *
 * Define each signature once per translation unit.
 */
#define FN_DEFINE(...)                                                        \
    MONOMORPH_CAT(FN_DEFINE_, MONOMORPH_NARG(__VA_ARGS__))(__VA_ARGS__)
#define FN_STRUCT_(T, R, PARAMS)                                              \
    typedef struct T { void *self; R (*code) PARAMS; } T
#define FN_DEFINE_1(r)                                                        \
    FN_STRUCT_(Fn(r), r, (void *self))
#define FN_DEFINE_2(a, r)                                                     \
    FN_STRUCT_(Fn(a, r), r, (void *self, a))
#define FN_DEFINE_3(a, b, r)                                                  \
    FN_STRUCT_(Fn(a, b, r), r, (void *self, a, b))
#define FN_DEFINE_4(a, b, c, r)                                               \
    FN_STRUCT_(Fn(a, b, c, r), r, (void *self, a, b, c))
#define FN_DEFINE_5(a, b, c, d, r)                                            \
    FN_STRUCT_(Fn(a, b, c, d, r), r, (void *self, a, b, c, d))
#define FN_DEFINE_6(a, b, c, d, e, r)                                         \
    FN_STRUCT_(Fn(a, b, c, d, e, r), r, (void *self, a, b, c, d, e))
#define FN_DEFINE_7(a, b, c, d, e, f, r)                                      \
    FN_STRUCT_(Fn(a, b, c, d, e, f, r), r, (void *self, a, b, c, d, e, f))
#define FN_DEFINE_8(a, b, c, d, e, f, g, r)                                   \
    FN_STRUCT_(Fn(a, b, c, d, e, f, g, r), r, (void *self, a, b, c, d, e, f, g))
#define FN_DEFINE_MONOMORPH_TOO_MANY(...)                                     \
    _Static_assert(0, "FN_DEFINE: at most 7 argument types plus a result")


/* -------------------------------------------------------------------------- */
/* Binding methods                                                            */
/* -------------------------------------------------------------------------- */

/*
 * BIND(Fn(A1, ..., An, R), T, name);
 *
 *   Makes the method OVERLOAD(name, T) bindable as a Fn(A1, ..., An, R).
 *   Generates, in this translation unit:
 *
 *     static R lambdaǀnameꞏTǀ(void *self, A1, ..., An)     the adapter
 *     static Fn(...) fnǀnameꞏTǀ(T *self)                    used by FN
 *
 *   The method's self may be `const T *` or `T *`. Its signature is checked
 *   against the Fn's; a missing or mismatched method is a compile error.
 *   Write the signature as a literal Fn(...).
 *
 * FN(T, name, p)   the closure of method `name` over p, a `T *`.
 */
#define BIND(sig, T, name)  FN_BIND_(T, name, FN_SIG_ ## sig)
#define FN_SIG_Fn(...)      (__VA_ARGS__)
#define FN_BIND_(T, name, types)                                              \
    FN_APPLY_(MONOMORPH_CAT(FN_BIND_, MONOMORPH_NARG types),                  \
              (T, name, DYN_UNPAREN types))
#define FN_APPLY_(m, args)  m args

#define FN_BIND_1(T, name, r)                                                 \
    FN_BIND_GEN(T, name, r, Fn(r),                                            \
        (),                                                                   \
        (),                                                                   \
        ())
#define FN_BIND_2(T, name, a, r)                                              \
    FN_BIND_GEN(T, name, r, Fn(a, r),                                         \
        (, a),                                                                \
        (, a p_a),                                                            \
        (, p_a))
#define FN_BIND_3(T, name, a, b, r)                                           \
    FN_BIND_GEN(T, name, r, Fn(a, b, r),                                      \
        (, a, b),                                                             \
        (, a p_a, b p_b),                                                     \
        (, p_a, p_b))
#define FN_BIND_4(T, name, a, b, c, r)                                        \
    FN_BIND_GEN(T, name, r, Fn(a, b, c, r),                                   \
        (, a, b, c),                                                          \
        (, a p_a, b p_b, c p_c),                                              \
        (, p_a, p_b, p_c))
#define FN_BIND_5(T, name, a, b, c, d, r)                                     \
    FN_BIND_GEN(T, name, r, Fn(a, b, c, d, r),                                \
        (, a, b, c, d),                                                       \
        (, a p_a, b p_b, c p_c, d p_d),                                       \
        (, p_a, p_b, p_c, p_d))
#define FN_BIND_6(T, name, a, b, c, d, e, r)                                  \
    FN_BIND_GEN(T, name, r, Fn(a, b, c, d, e, r),                             \
        (, a, b, c, d, e),                                                    \
        (, a p_a, b p_b, c p_c, d p_d, e p_e),                                \
        (, p_a, p_b, p_c, p_d, p_e))
#define FN_BIND_7(T, name, a, b, c, d, e, f, r)                               \
    FN_BIND_GEN(T, name, r, Fn(a, b, c, d, e, f, r),                          \
        (, a, b, c, d, e, f),                                                 \
        (, a p_a, b p_b, c p_c, d p_d, e p_e, f p_f),                         \
        (, p_a, p_b, p_c, p_d, p_e, p_f))
#define FN_BIND_8(T, name, a, b, c, d, e, f, g, r)                            \
    FN_BIND_GEN(T, name, r, Fn(a, b, c, d, e, f, g, r),                       \
        (, a, b, c, d, e, f, g),                                              \
        (, a p_a, b p_b, c p_c, d p_d, e p_e, f p_f, g p_g),                  \
        (, p_a, p_b, p_c, p_d, p_e, p_f, p_g))
#define FN_BIND_MONOMORPH_TOO_MANY(...)                                       \
    _Static_assert(0, "BIND: at most 7 parameters")

#define FN_BIND_GEN(T, name, R, FT, TYPES, DECLS, ARGS)                       \
    _Static_assert(_Generic(&OVERLOAD(name, T),                               \
                       R (*)(const T * DYN_UNPAREN TYPES): 1,                 \
                       R (*)(T * DYN_UNPAREN TYPES): 1,                       \
                       default: 0),                                           \
                   "BIND(" #T ", " #name "): the method's signature"          \
                   " does not match the Fn");                                 \
    static R MONOMORPH_MANGLE(lambda, name, T)(void *self                     \
                                               DYN_UNPAREN DECLS) {           \
        DYN_RETURN(R) OVERLOAD(name, T)((T *)self DYN_UNPAREN ARGS);          \
    }                                                                         \
    static inline FT MONOMORPH_MANGLE(fn, name, T)(T *self) {                 \
        return (FT){ self, MONOMORPH_MANGLE(lambda, name, T) };               \
    }                                                                         \
    static R MONOMORPH_MANGLE(lambda, name, T)(void *self                     \
                                               DYN_UNPAREN TYPES)

#define FN(T, name, p)  MONOMORPH_MANGLE(fn, name, T)(p)


/* -------------------------------------------------------------------------- */
/* Calling                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * CALL(f, args...)   calls closure f, a Fn value, with up to 7 arguments.
 * CALL(f)            calls a closure that takes no arguments.
 * f is evaluated twice, so pass a variable.
 */
#define CALL(...)                                                             \
    MONOMORPH_CAT(FN_CALL_, DYN_HAS_PARAMS(~, __VA_ARGS__))(__VA_ARGS__)
#define FN_CALL_0(f)       ((f).code((f).self))
#define FN_CALL_1(f, ...)  ((f).code((f).self, __VA_ARGS__))

#endif /* FN_H */