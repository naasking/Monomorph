/*
 * dyn.h
 *
 * Interfaces with dynamic dispatch through two-pointer values, an object
 * pointer plus a method table, in the style of Rust's `dyn Trait`. Types
 * stay plain structs, and any translation unit can implement any interface
 * for any type.
 *
 * Methods are ordinary overloads: a function named OVERLOAD(name, T) whose
 * first parameter is a `const T *` or `T *`.
 *
 *   #define area(T)   OVERLOAD(area, T)
 *   #define scale(T)  OVERLOAD(scale, T)
 *
 *   #define Shape_METHODS     \         (result, name, params...)
 *       (Dbl,  area),         \
 *       (void, scale, Dbl)
 *   INTERFACE(Shape);                       declares Dyn(Shape)
 *
 *   Dbl area(Circle)(const Circle *self) { return PI * self->r * self->r; }
 *   void scale(Circle)(Circle *self, Dbl k) { self->r *= k; }
 *   IMPL(Shape, Circle);                    implements Shape for Circle
 *
 *   Dyn(Shape) s = DYN(Shape, Circle, &c);
 *   area(Dyn(Shape))(&s);                   dispatching call
 *   area(Circle)(&c);                       statically bound call
 *
 * Generated code never writes `name(`, so method-name macros like the ones
 * above don't interfere with it.
 */
#ifndef DYN_H
#define DYN_H

#include "monomorph.h"

#define Dyn(I)     TYPE(Dyn, I)        /* object pointer + method table */
#define Vtable(I)  TYPE(Vtable, I)     /* the method table type          */


/* -------------------------------------------------------------------------- */
/* Interfaces                                                                 */
/* -------------------------------------------------------------------------- */

/*
 * INTERFACE(I);   with I##_METHODS defined as a list of method signatures,
 *                 (R, name, A1, ..., An), not counting self.
 *
 * Defines Vtable(I), Dyn(I), and one dispatching function per method,
 *
 *   R OVERLOAD(name, Dyn(I))(const Dyn(I) *d, A1, ..., An)
 *
 * which calls the implementation the method table points to. Dispatching
 * functions take a pointer, like the methods themselves, so generic code
 * written against name(T) works for T = Circle and T = Dyn(Shape) alike.
 * Up to 16 methods, each with up to 7 parameters.
 */
#define INTERFACE(I)                                                          \
    typedef struct Vtable(I) Vtable(I);                                       \
    typedef struct Dyn(I) { void *self; const Vtable(I) *vt; } Dyn(I);        \
    struct Vtable(I) { DYN_EACH(DYN_SLOT, I, ~, I##_METHODS) };               \
    DYN_EACH(DYN_DISPATCH, I, ~, I##_METHODS)                                 \
    typedef struct Dyn(I) Dyn(I)


/* -------------------------------------------------------------------------- */
/* Implementations                                                            */
/* -------------------------------------------------------------------------- */

/*
 * IMPL(I, T);   implements interface I for type T, in this translation unit.
 *
 * Requires an overload OVERLOAD(name, T) for every method I lists, taking
 * `const T *` or `T *` and then the listed parameters, with the listed
 * result; a missing or mismatched method is a compile error. Generates a
 * static method table and the constructor used by DYN.
 *
 * DYN(I, T, p)   makes a Dyn(I) from p, a `T *`.
 */
#define IMPL(I, T)                                                            \
    DYN_EACH(DYN_CHECK, I, T, I##_METHODS)                                    \
    DYN_EACH(DYN_THUNK, I, T, I##_METHODS)                                    \
    static const Vtable(I) MONOMORPH_MANGLE(vtable, I, T) = {                 \
        DYN_EACH(DYN_INIT, I, T, I##_METHODS)                                 \
    };                                                                        \
    DYN_MAYBE_UNUSED                                                          \
    static inline Dyn(I) MONOMORPH_MANGLE(dyn, I, T)(T *self) {               \
        return (Dyn(I)){ self, &MONOMORPH_MANGLE(vtable, I, T) };             \
    }                                                                         \
    typedef struct Dyn(I) Dyn(I)

#define DYN(I, T, p)  MONOMORPH_MANGLE(dyn, I, T)(p)


/* -------------------------------------------------------------------------- */
/* Generators, one per method signature                                       */
/* -------------------------------------------------------------------------- */

/* Each receives (I, T, R, name, (, A...), (, A p...), (, p...)). */

#define DYN_SLOT_GEN(I, T, R, name, TYPES, DECLS, ARGS)                       \
    R (*name)(void *self DYN_UNPAREN TYPES);

#define DYN_DISPATCH_GEN(I, T, R, name, TYPES, DECLS, ARGS)                   \
    DYN_MAYBE_UNUSED                                                          \
    static inline R OVERLOAD(name, Dyn(I))(const Dyn(I) *dyn_                 \
                                           DYN_UNPAREN DECLS) {               \
        DYN_RETURN(R) (dyn_->vt->name)(dyn_->self DYN_UNPAREN ARGS);          \
    }

#define DYN_CHECK_GEN(I, T, R, name, TYPES, DECLS, ARGS)                      \
    _Static_assert(_Generic(&OVERLOAD(name, T),                               \
                       R (*)(const T * DYN_UNPAREN TYPES): 1,                 \
                       R (*)(T * DYN_UNPAREN TYPES): 1,                       \
                       default: 0),                                           \
                   "IMPL(" #I ", " #T "): method " #name                      \
                   " does not match the interface's signature");

#define DYN_THUNK_GEN(I, T, R, name, TYPES, DECLS, ARGS)                      \
    static R MONOMORPH_MANGLE(name, I, T)(void *self DYN_UNPAREN DECLS) {     \
        DYN_RETURN(R) OVERLOAD(name, T)((T *)self DYN_UNPAREN ARGS);          \
    }

#define DYN_INIT_GEN(I, T, R, name, TYPES, DECLS, ARGS)                       \
    .name = MONOMORPH_MANGLE(name, I, T),

#define DYN_SLOT(I, T, m)      DYN_WITH_SIG(DYN_SLOT_GEN, I, T, m)
#define DYN_DISPATCH(I, T, m)  DYN_WITH_SIG(DYN_DISPATCH_GEN, I, T, m)
#define DYN_CHECK(I, T, m)     DYN_WITH_SIG(DYN_CHECK_GEN, I, T, m)
#define DYN_THUNK(I, T, m)     DYN_WITH_SIG(DYN_THUNK_GEN, I, T, m)
#define DYN_INIT(I, T, m)      DYN_WITH_SIG(DYN_INIT_GEN, I, T, m)


/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

/* Apply G to (I, T) and the method signature m, split into parts. */
#define DYN_WITH_SIG(G, I, T, m)  DYN_APPLY_(G, (I, T, DYN_SIG(m)))
#define DYN_SIG(m)  DYN_APPLY2_(MONOMORPH_CAT(DYN_SIG_, DYN_NARG m), m)
#define DYN_APPLY_(m, args)   m args
#define DYN_APPLY2_(m, args)  m args

#define DYN_SIG_2(R, name)                                                    \
    R, name,                                                                  \
    (),                                                                       \
    (),                                                                       \
    ()
#define DYN_SIG_3(R, name, a)                                                 \
    R, name,                                                                  \
    (, a),                                                                    \
    (, a p_a),                                                                \
    (, p_a)
#define DYN_SIG_4(R, name, a, b)                                              \
    R, name,                                                                  \
    (, a, b),                                                                 \
    (, a p_a, b p_b),                                                         \
    (, p_a, p_b)
#define DYN_SIG_5(R, name, a, b, c)                                           \
    R, name,                                                                  \
    (, a, b, c),                                                              \
    (, a p_a, b p_b, c p_c),                                                  \
    (, p_a, p_b, p_c)
#define DYN_SIG_6(R, name, a, b, c, d)                                        \
    R, name,                                                                  \
    (, a, b, c, d),                                                           \
    (, a p_a, b p_b, c p_c, d p_d),                                           \
    (, p_a, p_b, p_c, p_d)
#define DYN_SIG_7(R, name, a, b, c, d, e)                                     \
    R, name,                                                                  \
    (, a, b, c, d, e),                                                        \
    (, a p_a, b p_b, c p_c, d p_d, e p_e),                                    \
    (, p_a, p_b, p_c, p_d, p_e)
#define DYN_SIG_8(R, name, a, b, c, d, e, f)                                  \
    R, name,                                                                  \
    (, a, b, c, d, e, f),                                                     \
    (, a p_a, b p_b, c p_c, d p_d, e p_e, f p_f),                             \
    (, p_a, p_b, p_c, p_d, p_e, p_f)
#define DYN_SIG_9(R, name, a, b, c, d, e, f, g)                               \
    R, name,                                                                  \
    (, a, b, c, d, e, f, g),                                                  \
    (, a p_a, b p_b, c p_c, d p_d, e p_e, f p_f, g p_g),                      \
    (, p_a, p_b, p_c, p_d, p_e, p_f, p_g)
#define DYN_SIG_MANY_(...)                                                    \
    DYN_ERROR_at_most_7_parameters_per_method, dyn_error_, (), (), ()
#define DYN_SIG_10(...)  DYN_SIG_MANY_()
#define DYN_SIG_11(...)  DYN_SIG_MANY_()
#define DYN_SIG_12(...)  DYN_SIG_MANY_()
#define DYN_SIG_13(...)  DYN_SIG_MANY_()
#define DYN_SIG_14(...)  DYN_SIG_MANY_()
#define DYN_SIG_15(...)  DYN_SIG_MANY_()
#define DYN_SIG_16(...)  DYN_SIG_MANY_()
#define DYN_SIG_17(...)  DYN_SIG_MANY_()
#define DYN_SIG_18(...)  DYN_SIG_MANY_()
#define DYN_SIG_19(...)  DYN_SIG_MANY_()
#define DYN_SIG_20(...)  DYN_SIG_MANY_()
#define DYN_SIG_21(...)  DYN_SIG_MANY_()
#define DYN_SIG_22(...)  DYN_SIG_MANY_()
#define DYN_SIG_23(...)  DYN_SIG_MANY_()
#define DYN_SIG_24(...)  DYN_SIG_MANY_()

/* F(I, T, x) for each method signature x. */
#define DYN_EACH(F, I, T, ...)                                                \
    MONOMORPH_CAT(DYN_EACH_, DYN_NARG(__VA_ARGS__))(F, I, T, __VA_ARGS__)
#define DYN_EACH_1(F, I, T,                                                   \
                    x1)                                                       \
    F(I, T, x1)
#define DYN_EACH_2(F, I, T,                                                   \
                    x1, x2)                                                   \
    F(I, T, x1)                                                               \
    F(I, T, x2)
#define DYN_EACH_3(F, I, T,                                                   \
                    x1, x2, x3)                                               \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)
#define DYN_EACH_4(F, I, T,                                                   \
                    x1, x2, x3, x4)                                           \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)
#define DYN_EACH_5(F, I, T,                                                   \
                    x1, x2, x3, x4, x5)                                       \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)
#define DYN_EACH_6(F, I, T,                                                   \
                    x1, x2, x3, x4, x5, x6)                                   \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)
#define DYN_EACH_7(F, I, T,                                                   \
                    x1, x2, x3, x4, x5, x6, x7)                               \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)
#define DYN_EACH_8(F, I, T,                                                   \
                    x1, x2, x3, x4, x5, x6, x7, x8)                           \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)
#define DYN_EACH_9(F, I, T,                                                   \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9)                       \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)
#define DYN_EACH_10(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10)                  \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)
#define DYN_EACH_11(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11)             \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)
#define DYN_EACH_12(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12)        \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)                                                              \
    F(I, T, x12)
#define DYN_EACH_13(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13)   \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)                                                              \
    F(I, T, x12)                                                              \
    F(I, T, x13)
#define DYN_EACH_14(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13,   \
                    x14)                                                      \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)                                                              \
    F(I, T, x12)                                                              \
    F(I, T, x13)                                                              \
    F(I, T, x14)
#define DYN_EACH_15(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13,   \
                    x14, x15)                                                 \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)                                                              \
    F(I, T, x12)                                                              \
    F(I, T, x13)                                                              \
    F(I, T, x14)                                                              \
    F(I, T, x15)
#define DYN_EACH_16(F, I, T,                                                  \
                    x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13,   \
                    x14, x15, x16)                                            \
    F(I, T, x1)                                                               \
    F(I, T, x2)                                                               \
    F(I, T, x3)                                                               \
    F(I, T, x4)                                                               \
    F(I, T, x5)                                                               \
    F(I, T, x6)                                                               \
    F(I, T, x7)                                                               \
    F(I, T, x8)                                                               \
    F(I, T, x9)                                                               \
    F(I, T, x10)                                                              \
    F(I, T, x11)                                                              \
    F(I, T, x12)                                                              \
    F(I, T, x13)                                                              \
    F(I, T, x14)                                                              \
    F(I, T, x15)                                                              \
    F(I, T, x16)
#define DYN_EACH_MANY_(...)                                                   \
    _Static_assert(0, "INTERFACE: at most 16 methods");
#define DYN_EACH_17(...)  DYN_EACH_MANY_()
#define DYN_EACH_18(...)  DYN_EACH_MANY_()
#define DYN_EACH_19(...)  DYN_EACH_MANY_()
#define DYN_EACH_20(...)  DYN_EACH_MANY_()
#define DYN_EACH_21(...)  DYN_EACH_MANY_()
#define DYN_EACH_22(...)  DYN_EACH_MANY_()
#define DYN_EACH_23(...)  DYN_EACH_MANY_()
#define DYN_EACH_24(...)  DYN_EACH_MANY_()

#define DYN_NARG(...)                                                         \
    DYN_NARG_(__VA_ARGS__, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13,    \
              12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define DYN_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, \
                  _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, N, ...)  N

/* 0 for (~, f), 1 for (~, f, args...); used by fn.h's CALL. */
#define DYN_HAS_PARAMS(...)                                                   \
    MONOMORPH_CAT(DYN_HAS_PARAMS_, DYN_NARG(__VA_ARGS__))
#define DYN_HAS_PARAMS_2   0
#define DYN_HAS_PARAMS_3   1
#define DYN_HAS_PARAMS_4   1
#define DYN_HAS_PARAMS_5   1
#define DYN_HAS_PARAMS_6   1
#define DYN_HAS_PARAMS_7   1
#define DYN_HAS_PARAMS_8   1
#define DYN_HAS_PARAMS_9   1

#define DYN_UNPAREN(...)  __VA_ARGS__

/* Marks generated static inline functions a translation unit may not use.
   Clang warns about unused ones outside headers, and the macros generating
   them expand where they are used. */
#if defined(__GNUC__) || defined(__clang__)
#  define DYN_MAYBE_UNUSED  __attribute__((unused))
#else
#  define DYN_MAYBE_UNUSED
#endif

/* `return` unless R is void. The () only invokes DYN_IS_VOID_void when
   nothing follows `void`, so `void *` is not void. */
#define DYN_RETURN(R)     MONOMORPH_CAT(DYN_RETURN_, DYN_IS_VOID(R))
#define DYN_RETURN_0      return
#define DYN_RETURN_1
#define DYN_IS_VOID(R)    DYN_SECOND(MONOMORPH_CAT(DYN_IS_VOID_, R) ())
#define DYN_IS_VOID_void()        ~, 1
#define DYN_SECOND(...)           DYN_SECOND_(__VA_ARGS__, 0, ~)
#define DYN_SECOND_(a, b, ...)    b

#endif /* DYN_H */
