/*
 * fn.h
 *
 * Closures as two-pointer values, an environment pointer plus a code
 * pointer: the one-method case of dyn.h's interfaces. An environment is any
 * struct, and a closure's code is any method of it, i.e. any overload
 * OVERLOAD(name, T) taking a `const T *` or `T *` first, so every method can
 * also be used as a closure. Closure types are written like the methods they
 * call, and a closure is called like any method, through apply:
 *
 *   FN_DEFINE(Dbl FARGS(Dbl, Dbl));          defines Fn(Dbl FARGS(Dbl, Dbl))
 *
 *   #define apply(T)  OVERLOAD(apply, T)
 *
 *   typedef struct { Dbl w1, w2; } Weighted;
 *   Dbl apply(Weighted)(const Weighted *self, Dbl x, Dbl y) {
 *       return self->w1 * x + self->w2 * y;
 *   }
 *   DELEGATE(Weighted, METHOD(apply, Dbl FARGS(Dbl, Dbl)));
 *
 *   Weighted w = { 0.25, 0.75 };
 *   Fn(Dbl FARGS(Dbl, Dbl)) f = FN(Weighted, apply, &w);
 *   apply(Fn(Dbl FARGS(Dbl, Dbl)))(&f, 4, 8);      as a method
 *   CALL(f, 4, 8);                                 the same, shorter
 *
 * Code generic over apply(T) so takes either a concrete type, called
 * directly, or any closure with the same signature.
 *
 * A Fn borrows its environment: it must not outlive the object it was made
 * from. To return a closure, return the environment struct and FN it where
 * it is used.
 */
#ifndef FN_H
#define FN_H

#include "dyn.h"               /* METHOD, FARGS and helpers */


/* -------------------------------------------------------------------------- */
/* Closure types                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Fn(R FARGS(A1, ..., An))   the closure type, n = 0..7, its signature
 *                            written as in METHOD
 *
 * FN_DECLARE(R FARGS(A1, ..., An));   declares it
 * FN_DEFINE(R FARGS(A1, ..., An));    defines it, with its apply method:
 *
 *   struct Fn(...) {
 *       void *self;                               the environment
 *       R (*code)(void *self, A1, ..., An);       what to call with it
 *   };
 *   static inline R apply(Fn(...))(const Fn(...) *f, A1, ..., An);
 *
 * FN_DECLARE can be repeated, so headers declare the signatures they name
 * and combine freely. A declared Fn can appear in function declarations,
 * behind pointers and in extern declarations. Anything that needs its size
 * or members needs it defined: FN, CALL, apply, DELEGATE, passing a closure,
 * a function definition taking or returning one, or a struct member
 * holding one.
 *
 * FN_DEFINE appears once per signature in each translation unit that needs
 * the definition; a second one is a redefinition error. Every definition of
 * a signature is identical, so translation units that each define it agree.
 *
 * Types are named by how they are spelled, so give a closure type a short
 * name with a macro, `#define DblFn Fn(Dbl FARGS(Dbl))`. After a typedef,
 * apply(DblFn) would look for a method of the type DblFn.
 */
#define Fn(...)  TYPE(Fn, FN_SIG_(__VA_ARGS__))

/* R FARGS(A1, ..., An)  ->  R, A1, ..., An, checked as METHOD checks it */
#define FN_SIG_(...)           FN_DROP_NAME_I_(METHOD(~, __VA_ARGS__))
#define FN_DROP_NAME_I_(t)     FN_DROP_NAME_ t
#define FN_DROP_NAME_(x, ...)  __VA_ARGS__

/* The Fn of a signature's parts, as DYN_WITH_SIG gives them. */
#define FN_NAME_(R, TYPES)     TYPE(Fn, R DYN_UNPAREN TYPES)

#define FN_DECLARE(...)  typedef struct Fn(__VA_ARGS__) Fn(__VA_ARGS__)
#define FN_DEFINE(...)                                                        \
    DYN_WITH_SIG(FN_DEFINE_GEN, ~, ~, METHOD(apply, __VA_ARGS__))
#define FN_DEFINE_GEN(I, T, R, name, TYPES, DECLS, PASS)                      \
    typedef struct FN_NAME_(R, TYPES) {                                       \
        void *self;                                                           \
        R (*code)(void *self DYN_UNPAREN TYPES);                              \
    } FN_NAME_(R, TYPES);                                                     \
    DYN_MAYBE_UNUSED                                                          \
    static inline R OVERLOAD(name, FN_NAME_(R, TYPES))(                       \
            const FN_NAME_(R, TYPES) *fn_ DYN_UNPAREN DECLS) {                \
        DYN_RETURN(R) fn_->code(fn_->self DYN_UNPAREN PASS);                  \
    }                                                                         \
    typedef struct FN_NAME_(R, TYPES) FN_NAME_(R, TYPES)


/* -------------------------------------------------------------------------- */
/* Methods as closures                                                        */
/* -------------------------------------------------------------------------- */

/*
 * DELEGATE(T, METHOD(name, R FARGS(A1, ..., An)), ...);
 *
 *   Makes each listed method OVERLOAD(name, T) usable as the code of a
 *   Fn(R FARGS(A1, ..., An)), defined by FN_DEFINE. An interface's method
 *   list can be passed as is: DELEGATE(Circle, Shape_METHODS). For each
 *   method, generates in this translation unit:
 *
 *     static R lambdaǀnameꞏTǀ(void *self, A1, ..., An)     the adapter
 *     static Fn(...) fnǀnameꞏTǀ(T *self)                    used by FN
 *
 *   The method's self may be `const T *` or `T *`. Its signature is checked
 *   against the listed one; a missing or mismatched method is a compile
 *   error. Up to 16 methods, each with up to 7 parameters.
 *
 * FN(T, name, p)   the closure of method `name` over p, a `T *`.
 */
#define DELEGATE(T, ...)                                                      \
    DYN_EACH(FN_DELEGATE_, ~, T, __VA_ARGS__)                                 \
    _Static_assert(1, "DELEGATE")
#define FN_DELEGATE_(I, T, m)  DYN_WITH_SIG(FN_DELEGATE_GEN, I, T, m)
#define FN_DELEGATE_GEN(I, T, R, name, TYPES, DECLS, PASS)                    \
    _Static_assert(_Generic(&OVERLOAD(name, T),                               \
                       R (*)(const T * DYN_UNPAREN TYPES): 1,                 \
                       R (*)(T * DYN_UNPAREN TYPES): 1,                       \
                       default: 0),                                           \
                   "DELEGATE(" #T "): method " #name                          \
                   " does not match its signature");                          \
    static R MONOMORPH_MANGLE(lambda, name, T)(void *self                     \
                                               DYN_UNPAREN DECLS) {           \
        DYN_RETURN(R) OVERLOAD(name, T)((T *)self DYN_UNPAREN PASS);          \
    }                                                                         \
    DYN_MAYBE_UNUSED                                                          \
    static inline FN_NAME_(R, TYPES) MONOMORPH_MANGLE(fn, name, T)(T *self) { \
        return (FN_NAME_(R, TYPES)){ self,                                    \
                                     MONOMORPH_MANGLE(lambda, name, T) };     \
    }

#define FN(T, name, p)  MONOMORPH_MANGLE(fn, name, T)(p)


/* -------------------------------------------------------------------------- */
/* Calling                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * apply(Fn(...))(&f, args...)   calls closure f as a method, so generic code
 *                               over apply(T) works for closures too. Needs
 *                               the method-name macro, as above.
 * CALL(f, args...)              the same without naming f's type, with up to
 * CALL(f)                       7 arguments. f is evaluated twice, so pass a
 *                               variable.
 */
#define CALL(...)                                                             \
    MONOMORPH_CAT(FN_CALL_, DYN_HAS_PARAMS(~, __VA_ARGS__))(__VA_ARGS__)
#define FN_CALL_0(f)       ((f).code((f).self))
#define FN_CALL_1(f, ...)  ((f).code((f).self, __VA_ARGS__))

#endif /* FN_H */
