/*
 * dyn.h
 *
 * Interfaces with dynamic dispatch, in the style of C#'s interface types
 * and Rust's `dyn Trait`. An interface is a type: INTERFACE(IShape) defines
 * IShape, a two-pointer value holding any object that implements it, an
 * object pointer plus its type's method table. Name interfaces as C# does,
 * with an I prefix. Types stay plain structs, and any translation unit can
 * implement any interface for any type.
 *
 * Methods are ordinary overloads: a function named OVERLOAD(name, T) whose
 * first parameter is a `const T *` or `T *`.
 *
 *   #define area(T)   OVERLOAD(area, T)
 *   #define scale(T)  OVERLOAD(scale, T)
 *
 *   #define IShape_METHODS                                                   \
 *       METHOD(area,  Dbl  FARGS()),                                         \
 *       METHOD(scale, void FARGS(Dbl))
 *   INTERFACE(IShape);                      defines the type IShape
 *
 *   Dbl area(Circle)(const Circle *self) { return PI * self->r * self->r; }
 *   void scale(Circle)(Circle *self, Dbl k) { self->r *= k; }
 *   IMPL(IShape, Circle);                   implements IShape for Circle
 *
 *   IShape s = DYN(IShape, Circle, &c);
 *   area(IShape)(&s);                       dispatching call
 *   VCALL(s, area);                         the same, through the table
 *   area(Circle)(&c);                       statically bound call
 *
 * Interfaces can be generic, like C#'s IComparer<T>: see INTERFACE_OF.
 *
 * Generated code never writes `name(`, so method-name macros like the ones
 * above don't interfere with it. It also uses only the prefixed
 * MONOMORPH_TYPE and MONOMORPH_OVERLOAD, so this header and fn.h work with
 * MONOMORPH_NO_SHORT_NAMES defined.
 */
#ifndef DYN_H
#define DYN_H

#include "monomorph.h"

#define Vtable(I)  MONOMORPH_TYPE(Vtable, I)  /* an interface's method table */


/* -------------------------------------------------------------------------- */
/* Interfaces                                                                 */
/* -------------------------------------------------------------------------- */

/*
 * METHOD(name, R FARGS(A1, ..., An))
 *
 *   A method named name taking A1, ..., An after self and returning R,
 *   written the way the method itself is defined:
 *
 *     void scale(Circle)(Circle *self, Dbl k)       the method
 *     METHOD(scale, void FARGS(Dbl))                its signature
 *
 *   FARGS() or FARGS(void) for none. Other macros can build or pass on a
 *   signature freely. One not of this form is reported as
 *   DYN_ERROR_signature_must_be_written_R_FARGS, and one with no result
 *   type as DYN_ERROR_FARGS_needs_a_result_type_before_it. fn.h writes
 *   closure types the same way, Fn(R FARGS(A1, ..., An)).
 */
#define METHOD(name, ...)  DYN_METHOD_(name, __VA_ARGS__, DYN_NO_FARGS_, ~)
#define FARGS(...)         , DYN_FARGS_(__VA_ARGS__)

/* FARGS leaves the marker DYN_FARGS_(...) after the result. METHOD checks
   that it is there with a result before it, and makes the signature into
   the tuple (name, R, A1, ..., An) the generators read. */
#define DYN_METHOD_(name, r, m, ...)                                          \
    DYN_METHOD_I_(name, r, DYN_FARGS_CHECK_ ## m)
#define DYN_METHOD_I_(name, r, ...)                                           \
    DYN_METHOD_J_(name, r, __VA_ARGS__, DYN_METHOD_BAD_, (), ~)
#define DYN_METHOD_J_(name, r, marker, kind, params, ...)                     \
    kind(name, r, params)
#define DYN_FARGS_CHECK_DYN_FARGS_(...)                                       \
    ~, DYN_METHOD_OK_,                                                        \
    (MONOMORPH_CAT(DYN_FARGS_LIST_, DYN_NO_ARGS(__VA_ARGS__))(__VA_ARGS__))
#define DYN_FARGS_LIST_0(...)  , __VA_ARGS__
#define DYN_FARGS_LIST_1(...)
#define DYN_METHOD_OK_(name, r, params)                                       \
    MONOMORPH_CAT(DYN_METHOD_RESULT_, DYN_IS_EMPTY(r))(name, r, params)
#define DYN_METHOD_RESULT_0(name, r, params)  (name, r DYN_UNPAREN params)
#define DYN_METHOD_RESULT_1(name, r, params)                                  \
    (name, DYN_ERROR_FARGS_needs_a_result_type_before_it)
#define DYN_METHOD_BAD_(name, r, params)                                      \
    (name, DYN_ERROR_signature_must_be_written_R_FARGS)

/*
 * INTERFACE(I);   with I##_METHODS defined as a list of METHODs.
 *
 * Defines the interface type I, a value of which holds an object and its
 * type's method table,
 *
 *   struct I { void *self; const Vtable(I) *vt; };
 *
 * and one dispatching function per method,
 *
 *   R OVERLOAD(name, I)(const I *i, A1, ..., An)
 *
 * which calls the implementation the method table points to. Dispatching
 * functions take a pointer, like the methods themselves, so generic code
 * written against name(T) works for T = Circle and T = IShape alike. The
 * table can also be called directly: see VCALL. Up to 16 methods, each with
 * up to 7 parameters.
 *
 * Name interfaces as C# does, with an I prefix: IShape, IComparer. The name
 * becomes the type, so it can't also name another type.
 *
 * INTERFACE_OF(G, A1, ..., An);   with G##_METHODS(A1, ..., An) defined
 *
 *   A generic interface G instantiated at A1, ..., An, as C#'s IComparer<T>
 *   is: an ordinary interface named G(A1, ..., An), whose method list
 *   depends on its arguments. Name it with a type-constructor macro.
 *
 *     #define IComparer(T)  TYPE(IComparer, T)
 *     #define IComparer_METHODS(T)                                           \
 *         METHOD(compare, int FARGS(const T *, const T *))
 *     INTERFACE_OF(IComparer, Str);       defines IComparer(Str)
 *
 *   Instantiate each once per translation unit.
 */
#define INTERFACE(I)  DYN_INTERFACE_(I, I##_METHODS)

#define INTERFACE_OF(G, ...)                                                  \
    DYN_INTERFACE_(MONOMORPH_TYPE(G, __VA_ARGS__), G##_METHODS(__VA_ARGS__))

/* The interface type I, its table type and dispatchers, from its methods. */
#define DYN_INTERFACE_(I, ...)                                                \
    typedef struct Vtable(I) Vtable(I);                                       \
    typedef struct I { void *self; const Vtable(I) *vt; } I;                  \
    struct Vtable(I) { DYN_EACH(DYN_SLOT, I, ~, __VA_ARGS__) };               \
    DYN_EACH(DYN_DISPATCH, I, ~, __VA_ARGS__)                                 \
    typedef struct I I


/* -------------------------------------------------------------------------- */
/* Implementations                                                            */
/* -------------------------------------------------------------------------- */

/*
 * IMPL(I, T);   implements interface I for type T, in this translation unit.
 *
 * Requires an overload OVERLOAD(name, T) for every method I lists, taking
 * `const T *` or `T *` and then the listed parameters, with the listed
 * result; a missing or mismatched method is a compile error. Generates a
 * method table, private to this translation unit, and the constructor used
 * by DYN.
 *
 * DYN(I, T, p)   makes an I from p, a `T *`.
 *
 * IMPL_DECLARE(I, T);   in a header: declares the table and defines the
 *                       constructor, so any file including it can use DYN
 * IMPL_DEFINE(I, T);    in one source file: checks the methods and defines
 *                       the table
 *
 *   The same, split for separate compilation, to reduce code bloat: a
 *   program has one table for I and T, at one address, instead of a
 *   private copy in every file that uses it. Files using the declaration
 *   need neither the methods nor IMPL. IMPL is the two in one file with
 *   the table kept private, so it can be repeated in several files, as the
 *   split can't. Use one form or the other for a given I and T.
 *
 * IMPL_OF(G, (A1, ..., An), T);          the same for a generic interface,
 * IMPL_OF_DEFINE(G, (A1, ..., An), T);   whose methods depend on A1, ..., An
 *
 *   IMPL_DECLARE(G(A1, ..., An), T) and DYN(G(A1, ..., An), T, p) serve
 *   generic interfaces as they are. A type implements a generic interface
 *   at one instantiation, since its methods are named for the type alone;
 *   a generic type, like Reverse(T), implements it once per instance.
 */
#define IMPL(I, T)                                                            \
    DYN_IMPL_TABLE_(static, I, T, I##_METHODS);                               \
    DYN_IMPL_CONSTRUCTOR_(I, T)

#define IMPL_DECLARE(I, T)                                                    \
    extern const Vtable(I) MONOMORPH_MANGLE(vtable, I, T);                    \
    DYN_IMPL_CONSTRUCTOR_(I, T)

#define IMPL_DEFINE(I, T)                                                     \
    DYN_IMPL_TABLE_(, I, T, I##_METHODS);                                     \
    typedef struct I I

#define IMPL_OF(G, ARGS, T)                                                   \
    DYN_IMPL_TABLE_(static, DYN_GENERIC_(G, ARGS), T, G##_METHODS ARGS);      \
    DYN_IMPL_CONSTRUCTOR_(DYN_GENERIC_(G, ARGS), T)

#define IMPL_OF_DEFINE(G, ARGS, T)                                            \
    DYN_IMPL_TABLE_(, DYN_GENERIC_(G, ARGS), T, G##_METHODS ARGS);            \
    typedef struct DYN_GENERIC_(G, ARGS) DYN_GENERIC_(G, ARGS)

#define DYN(I, T, p)  MONOMORPH_MANGLE(dyn, I, T)(p)

/* G(A1, ..., An), from G and (A1, ..., An). */
#define DYN_GENERIC_(G, ARGS)  MONOMORPH_TYPE(G, DYN_UNPAREN ARGS)

/* The checks, adapters and table, with the table's linkage. */
#define DYN_IMPL_TABLE_(linkage, I, T, ...)                                   \
    DYN_EACH(DYN_CHECK, I, T, __VA_ARGS__)                                    \
    DYN_EACH(DYN_THUNK, I, T, __VA_ARGS__)                                    \
    linkage const Vtable(I) MONOMORPH_MANGLE(vtable, I, T) = {                \
        DYN_EACH(DYN_INIT, I, T, __VA_ARGS__)                                 \
    }

/* The constructor DYN calls, which needs the table declared. */
#define DYN_IMPL_CONSTRUCTOR_(I, T)                                           \
    DYN_MAYBE_UNUSED                                                          \
    static inline I MONOMORPH_MANGLE(dyn, I, T)(T *self) {                    \
        return (I){ self, &MONOMORPH_MANGLE(vtable, I, T) };                  \
    }                                                                         \
    typedef struct I I


/* -------------------------------------------------------------------------- */
/* Calling through the table                                                  */
/* -------------------------------------------------------------------------- */

/*
 * VCALL(i, name, args...)   calls method name through interface value i's
 * VCALL(i, name)            table, as (i.vt->name)(i.self, args...). The
 *                           parentheses keep method-name macros like
 *                           area(T) from capturing the call, so write them
 *                           when calling a table without VCALL. i is
 *                           evaluated twice, so pass a variable.
 *
 * VTABLE(I, T)              the address of T's table for I, a constant, so
 *                           an interface value can also be initialized
 *                           directly, even statically:
 *                           { &c, VTABLE(IShape, Circle) }
 */
#define VCALL(...)                                                            \
    MONOMORPH_CAT(DYN_VCALL_, DYN_HAS_PARAMS(__VA_ARGS__))(__VA_ARGS__)
#define DYN_VCALL_0(i, name)       (((i).vt->name)((i).self))
#define DYN_VCALL_1(i, name, ...)  (((i).vt->name)((i).self, __VA_ARGS__))

#define VTABLE(I, T)  (&MONOMORPH_MANGLE(vtable, I, T))


/* -------------------------------------------------------------------------- */
/* Generators, one per method signature                                       */
/* -------------------------------------------------------------------------- */

/* Each receives (I, T, R, name, (, A...), (, A p...), (, p...)). */

#define DYN_SLOT_GEN(I, T, R, name, TYPES, DECLS, PASS)                       \
    R (*name)(void *self DYN_UNPAREN TYPES);

#define DYN_DISPATCH_GEN(I, T, R, name, TYPES, DECLS, PASS)                   \
    DYN_MAYBE_UNUSED                                                          \
    static inline R MONOMORPH_OVERLOAD(name, I)(                              \
            const I *iface_ DYN_UNPAREN DECLS) {                              \
        DYN_RETURN(R) (iface_->vt->name)(iface_->self DYN_UNPAREN PASS);      \
    }

#define DYN_CHECK_GEN(I, T, R, name, TYPES, DECLS, PASS)                      \
    _Static_assert(_Generic(&MONOMORPH_OVERLOAD(name, T),                     \
                       R (*)(const T * DYN_UNPAREN TYPES): 1,                 \
                       R (*)(T * DYN_UNPAREN TYPES): 1,                       \
                       default: 0),                                           \
                   "IMPL(" #I ", " #T "): method " #name                      \
                   " does not match the interface's signature");

#define DYN_THUNK_GEN(I, T, R, name, TYPES, DECLS, PASS)                      \
    static R MONOMORPH_MANGLE(name, I, T)(void *self DYN_UNPAREN DECLS) {     \
        DYN_RETURN(R)                                                         \
            MONOMORPH_OVERLOAD(name, T)((T *)self DYN_UNPAREN PASS);          \
    }

#define DYN_INIT_GEN(I, T, R, name, TYPES, DECLS, PASS)                       \
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

/* (name, R, A1, ..., An), as METHOD makes it  ->  the generators' parts */
#define DYN_SIG_2(name, R)                                                    \
    R, name,                                                                  \
    (),                                                                       \
    (),                                                                       \
    ()
#define DYN_SIG_3(name, R, a)                                                 \
    R, name,                                                                  \
    (, a),                                                                    \
    (, a p_a),                                                                \
    (, p_a)
#define DYN_SIG_4(name, R, a, b)                                              \
    R, name,                                                                  \
    (, a, b),                                                                 \
    (, a p_a, b p_b),                                                         \
    (, p_a, p_b)
#define DYN_SIG_5(name, R, a, b, c)                                           \
    R, name,                                                                  \
    (, a, b, c),                                                              \
    (, a p_a, b p_b, c p_c),                                                  \
    (, p_a, p_b, p_c)
#define DYN_SIG_6(name, R, a, b, c, d)                                        \
    R, name,                                                                  \
    (, a, b, c, d),                                                           \
    (, a p_a, b p_b, c p_c, d p_d),                                           \
    (, p_a, p_b, p_c, p_d)
#define DYN_SIG_7(name, R, a, b, c, d, e)                                     \
    R, name,                                                                  \
    (, a, b, c, d, e),                                                        \
    (, a p_a, b p_b, c p_c, d p_d, e p_e),                                    \
    (, p_a, p_b, p_c, p_d, p_e)
#define DYN_SIG_8(name, R, a, b, c, d, e, f)                                  \
    R, name,                                                                  \
    (, a, b, c, d, e, f),                                                     \
    (, a p_a, b p_b, c p_c, d p_d, e p_e, f p_f),                             \
    (, p_a, p_b, p_c, p_d, p_e, p_f)
#define DYN_SIG_9(name, R, a, b, c, d, e, f, g)                               \
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

/* 0 for (~, f), 1 for (~, f, args...); used by DYN_NO_ARGS, VCALL and fn.h's
   CALL. */
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
   nothing follows `void`, so `void *` is not void. An empty R counts as
   void too, which DYN_NO_ARGS relies on for FARGS(). */
#define DYN_RETURN(R)     MONOMORPH_CAT(DYN_RETURN_, DYN_IS_VOID(R))
#define DYN_RETURN_0      return
#define DYN_RETURN_1
#define DYN_IS_VOID(R)    DYN_SECOND(MONOMORPH_CAT(DYN_IS_VOID_, R) ())
#define DYN_IS_VOID_void()        ~, 1
#define DYN_IS_VOID_()            ~, 1
#define DYN_SECOND(...)           DYN_SECOND_(__VA_ARGS__, 0, ~)
#define DYN_SECOND_(a, b, ...)    b

/* 1 for an empty argument list, () or (void); 0 for any arguments. */
#define DYN_NO_ARGS(...)                                                      \
    MONOMORPH_CAT(DYN_NO_ARGS_, DYN_HAS_PARAMS(~, __VA_ARGS__))(__VA_ARGS__)
#define DYN_NO_ARGS_0(a)    DYN_IS_VOID(a)
#define DYN_NO_ARGS_1(...)  0

/* 1 if the first argument is empty. A type never starts with `(`, so the
   probe is only invoked when nothing comes before the (). */
#define DYN_IS_EMPTY(...)         DYN_IS_EMPTY_(DYN_FIRST_(__VA_ARGS__, ~))
#define DYN_IS_EMPTY_(a)          DYN_SECOND(DYN_EMPTY_PROBE_ a ())
#define DYN_EMPTY_PROBE_()        ~, 1
#define DYN_FIRST_(a, ...)        a

#endif /* DYN_H */
