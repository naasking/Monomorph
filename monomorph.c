#ifndef MONOMORPH_H
#define MONOMORPH_H

/*
 * monomorph.h
 *
 * Mimics high-level abstractions in C through monomorphisation:
 * parametric types, type-constructor polymorphism and type-class-style
 * overloading. Every instantiation is a concrete C type or function with
 * a readable mangled name; there is no runtime dispatch.
 *
 * Surface conventions:
 *
 *   #define List(T)         TYPE(List, T)              type constructor
 *   #define eq(T)           OVERLOAD(eq, T)            overloaded method
 *   #define contains(F, T)  OVERLOAD(contains, F, T)   F: a type constructor
 *
 * Concrete names become:
 *
 *   List(Str)               ListǀStrǀ
 *   eq(Str)                 eqǀStrǀ
 *   contains(Matrix, int)   containsǀMatrixꞏintǀ
 *
 * Nested applications remain structurally visible:
 *
 *   List(Matrix(int))               ListǀMatrixǀintǀǀ
 *   Pair(Matrix(FP16), List(BF16))  PairǀMatrixǀFP16ǀꞏListǀBF16ǀǀ
 *
 * A type constructor passed as a parameter is mangled by name, and can be
 * applied inside an implementation:  #define F List  ...  F(T) xs;
 *
 * Requirements:
 *
 *   - C11 or later (U+A78F is not a valid identifier character in
 *     C99), UTF-8 source. GCC >= 10, Clang, or MSVC with /utf-8
 *     and /Zc:preprocessor.
 *
 *   - Every type argument must expand to exactly ONE token: a typedef
 *     name or a single-keyword type such as `float`. `unsigned int`,
 *     `float *` and `struct foo` cannot be pasted. Arguments are
 *     macro-expanded before mangling, so a type argument that is itself
 *     a macro is mangled under its expansion.
 *
 *   - GCC prints these names as-is in diagnostics only under a UTF-8
 *     locale; under LANG=C / POSIX it prints \U000001c0 / \U0000a78f
 *     escapes.
 *
 * Define MONOMORPH_NO_SHORT_NAMES before inclusion to suppress the TYPE /
 * OVERLOAD aliases and use MONOMORPH_TYPE / MONOMORPH_OVERLOAD only.
 */


/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/*
 * Open / close:  ǀ   U+01C0 LATIN LETTER DENTAL CLICK      (XID_Start)
 * Separator:     ꞏ   U+A78F LATIN LETTER SINOLOGICAL DOT   (XID_Start)
 *
 * Each delimiter is a complete identifier on its own, so every
 * intermediate paste result is a valid identifier.
 *
 * Neither character occurs in ordinary type names, so mangling is
 * injective and the structure can be read back unambiguously: ǀ followed
 * by a name opens a list; ǀ followed by ꞏ, ǀ, or end-of-name closes one.
 *
 *   Pair(Vec_FP32, Mask)  ->  PairǀVec_FP32ꞏMaskǀ
 *   Pair(Vec, FP32_Mask)  ->  PairǀVecꞏFP32_Maskǀ
 *
 * Most coding fonts lack both glyphs, so editors draw them from a
 * fallback font. VS Code's ambiguous-character highlighting flags ǀ
 * (as a look-alike of I) unless it is allow-listed.
 *
 * These are macros, so they must only ever reach ## through a
 * MONOMORPH_CAT* wrapper. Writing `x##MONOMORPH_OPEN` pastes the literal
 * spelling "MONOMORPH_OPEN".
 */

#define MONOMORPH_OPEN   ǀ
#define MONOMORPH_SEP    ꞏ
#define MONOMORPH_CLOSE  ǀ


/* -------------------------------------------------------------------------- */
/* Basic token operations                                                     */
/* -------------------------------------------------------------------------- */

/*
 * The outer macro forces full expansion of every argument (including the
 * delimiters and nested type applications) before the inner macro
 * applies ##.
 */

#define MONOMORPH_CAT_I(a, b)          a##b
#define MONOMORPH_CAT(a, b)            MONOMORPH_CAT_I(a, b)

#define MONOMORPH_CAT3_I(a, b, c)      a##b##c
#define MONOMORPH_CAT3(a, b, c)        MONOMORPH_CAT3_I(a, b, c)

#define MONOMORPH_CAT4_I(a, b, c, d)   a##b##c##d
#define MONOMORPH_CAT4(a, b, c, d)     MONOMORPH_CAT4_I(a, b, c, d)


/* -------------------------------------------------------------------------- */
/* Stringification                                                            */
/* -------------------------------------------------------------------------- */

#define MONOMORPH_STRING_I(x) #x
#define MONOMORPH_STRING(x)   MONOMORPH_STRING_I(x)


/* -------------------------------------------------------------------------- */
/* Variadic argument count                                                    */
/* -------------------------------------------------------------------------- */

/*
 * Supported arity: 1..8. Arities 9..16 select
 * MONOMORPH_JOIN_MONOMORPH_TOO_MANY, which produces a name containing
 * MONOMORPH_ERROR_more_than_8_type_parameters so the
 * compiler's "unknown type name" error says what went wrong.
 *
 * The trailing 0 keeps MONOMORPH_NARG_I's `...` non-empty, which pre-C23
 * compilers warn about under -pedantic.
 *
 * Zero arguments cannot be distinguished from one empty argument here;
 * `TYPE(List)` yields `Listǀǀ` without a diagnostic.
 */

#define MONOMORPH_NARG(...)                                                   \
    MONOMORPH_NARG_I(__VA_ARGS__,                                             \
        MONOMORPH_TOO_MANY, MONOMORPH_TOO_MANY,                               \
        MONOMORPH_TOO_MANY, MONOMORPH_TOO_MANY,                               \
        MONOMORPH_TOO_MANY, MONOMORPH_TOO_MANY,                               \
        MONOMORPH_TOO_MANY, MONOMORPH_TOO_MANY,                               \
        8, 7, 6, 5, 4, 3, 2, 1, 0)

#define MONOMORPH_NARG_I(                                                     \
    _1, _2, _3, _4, _5, _6, _7, _8,                                           \
    _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N


/* -------------------------------------------------------------------------- */
/* Parameter list joining                                                     */
/* -------------------------------------------------------------------------- */

/*
 *     MONOMORPH_JOIN_3(A, B, C)  ->  AꞏBꞏC
 *
 * Each step pastes three already-expanded identifiers, so no paste ever
 * touches punctuation.
 */

#define MONOMORPH_JOIN_1(a)                                                   \
    a
#define MONOMORPH_JOIN_2(a, b)                                                \
    MONOMORPH_CAT3(a, MONOMORPH_SEP, b)
#define MONOMORPH_JOIN_3(a, b, c)                                             \
    MONOMORPH_CAT3(MONOMORPH_JOIN_2(a, b), MONOMORPH_SEP, c)
#define MONOMORPH_JOIN_4(a, b, c, d)                                          \
    MONOMORPH_CAT3(MONOMORPH_JOIN_3(a, b, c), MONOMORPH_SEP, d)
#define MONOMORPH_JOIN_5(a, b, c, d, e)                                       \
    MONOMORPH_CAT3(MONOMORPH_JOIN_4(a, b, c, d), MONOMORPH_SEP, e)
#define MONOMORPH_JOIN_6(a, b, c, d, e, f)                                    \
    MONOMORPH_CAT3(MONOMORPH_JOIN_5(a, b, c, d, e), MONOMORPH_SEP, f)
#define MONOMORPH_JOIN_7(a, b, c, d, e, f, g)                                 \
    MONOMORPH_CAT3(MONOMORPH_JOIN_6(a, b, c, d, e, f), MONOMORPH_SEP, g)
#define MONOMORPH_JOIN_8(a, b, c, d, e, f, g, h)                              \
    MONOMORPH_CAT3(MONOMORPH_JOIN_7(a, b, c, d, e, f, g), MONOMORPH_SEP, h)

#define MONOMORPH_JOIN_MONOMORPH_TOO_MANY(...)                                \
    MONOMORPH_ERROR_more_than_8_type_parameters

#define MONOMORPH_JOIN(...)                                                   \
    MONOMORPH_CAT(MONOMORPH_JOIN_, MONOMORPH_NARG(__VA_ARGS__))(__VA_ARGS__)


/* -------------------------------------------------------------------------- */
/* Mangling                                                                   */
/* -------------------------------------------------------------------------- */

/*
 *     MONOMORPH_MANGLE(Name, A, B)  ->  NameǀAꞏBǀ
 */

#define MONOMORPH_MANGLE(name, ...)                                           \
    MONOMORPH_CAT4(name, MONOMORPH_OPEN,                                      \
                   MONOMORPH_JOIN(__VA_ARGS__), MONOMORPH_CLOSE)


/* -------------------------------------------------------------------------- */
/* Public forms                                                               */
/* -------------------------------------------------------------------------- */

#define MONOMORPH_TYPE(Ctor, ...)      MONOMORPH_MANGLE(Ctor, __VA_ARGS__)
#define MONOMORPH_OVERLOAD(Name, ...)  MONOMORPH_MANGLE(Name, __VA_ARGS__)

#ifndef MONOMORPH_NO_SHORT_NAMES
#  define TYPE(Ctor, ...)            MONOMORPH_MANGLE(Ctor, __VA_ARGS__)
#  define OVERLOAD(Name, ...)        MONOMORPH_MANGLE(Name, __VA_ARGS__)
#endif


/* -------------------------------------------------------------------------- */
/* Diagnostic helpers                                                         */
/* -------------------------------------------------------------------------- */

#define MONOMORPH_TYPE_NAME(Ctor, ...)                                        \
    MONOMORPH_STRING(MONOMORPH_TYPE(Ctor, __VA_ARGS__))

#define MONOMORPH_OVERLOAD_NAME(Name, ...)                                    \
    MONOMORPH_STRING(MONOMORPH_OVERLOAD(Name, __VA_ARGS__))


#endif /* MONOMORPH_H */
