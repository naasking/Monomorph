/*
 * A tagless-final interpreter for the simply typed lambda calculus with
 * integers (Carette, Kiselyov and Shan, "Finally Tagless, Partially
 * Evaluated").
 *
 * The language is an interface, Lang, with a method per construct, and an
 * interpreter is an implementation of it: Eval computes a term's value and
 * Show prints it. A term is a C function taking any implementation, through
 * Dyn(Lang), so each is compiled once and run by both.
 *
 * Lambdas are higher-order abstract syntax: lam takes its body as a closure
 * from the variable to the body, so the object language's variables are C
 * variables and substitution is a C call.
 *
 * Its types are C types: a term of type A is a Repr(A), and an ill-typed
 * term doesn't compile. What a Repr holds is up to its interpreter, an int
 * or a closure for Eval and text for Show, and as its type says which, no
 * interpreter needs tags to find out.
 *
 * The file is in three parts: the core, defining the language and how to
 * write terms in it; the interpreters, implementing it; and examples,
 * terms written against the core alone and run by both interpreters.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fn.h"


/* -------------------------------------------------------------------------- */
/* Core: memory                                                               */
/* -------------------------------------------------------------------------- */

/* Closures' environments and interpreters' results are shared freely, so
   they are never freed; the program is short. */
static void *alloc(size_t n) {
    void *p = malloc(n);
    if (!p) abort();
    return p;
}

static void *copy(const void *v, size_t n) {
    return memcpy(alloc(n), v, n);
}

/* A new T on the heap, initialized with the given members. */
#define NEW(T, ...)  ((T *)copy(&(T){ __VA_ARGS__ }, sizeof(T)))


/* -------------------------------------------------------------------------- */
/* Core: types                                                                */
/* -------------------------------------------------------------------------- */

/* Object-language types are names, Int and Arr(A, B) for A -> B, used only
   inside other names. Repr(A) is a term of type A as some interpreter
   represents it: a pointer to that interpreter's own data. */
#define Arr(A, B)       TYPE(Arr, A, B)
#define Repr(A)         TYPE(Repr, A)
#define DEFINE_REPR(A)  typedef struct Repr(A) { void *p; } Repr(A)

/* The body of a lambda from A to B: a closure from its variable to it. */
#define Body(A, B)      Fn(Repr(B) FARGS(Repr(A)))

/* C has no generic methods, so lam and app have an instance for each
   function type the program uses, listed here, each after the types it is
   made of. This is the one part of the core that depends on the examples:
   a term at a new function type needs its type added here, and every
   interpreter then implements it. */
#define ARROWS(X)                                                             \
    X(Int, Int)                          /* Int -> Int                 */     \
    X(Int, Arr(Int, Int))                /* Int -> Int -> Int          */     \
    X(Arr(Int, Int), Arr(Int, Int))      /* (Int -> Int) -> Int -> Int */

DEFINE_REPR(Int);

#define DEFINE_ARROW(A, B)                                                    \
    DEFINE_REPR(Arr(A, B));                                                   \
    FN_DEFINE(Repr(B) FARGS(Repr(A)));
ARROWS(DEFINE_ARROW)


/* -------------------------------------------------------------------------- */
/* Core: the language                                                         */
/* -------------------------------------------------------------------------- */

/* Method names. lam(A, B, T) and app(A, B, T) are the instances for A -> B,
   lamǀAꞏBǀ and appǀAꞏBǀ, of the implementation T. */
#define lit(T)        OVERLOAD(lit, T)
#define add(T)        OVERLOAD(add, T)
#define lam(A, B, T)  OVERLOAD(OVERLOAD(lam, A, B), T)
#define app(A, B, T)  OVERLOAD(OVERLOAD(app, A, B), T)
#define apply(T)      OVERLOAD(apply, T)

/* The language, as an interface:
 *
 *   lit(n)     the integer n                            Int
 *   add(a, b)  a + b, for a, b : Int                    Int
 *   lam(f)     \x. f(x), for f a Body(A, B)             A -> B
 *   app(f, x)  f x, for f : A -> B and x : A            B
 */
#define Lang_METHODS                                                          \
    METHOD(lit, Repr(Int) FARGS(int)),                                        \
    METHOD(add, Repr(Int) FARGS(Repr(Int), Repr(Int)))                        \
    ARROWS(LANG_ARROW_METHODS)
#define LANG_ARROW_METHODS(A, B)                                              \
    , METHOD(OVERLOAD(lam, A, B), Repr(Arr(A, B)) FARGS(Body(A, B)))          \
    , METHOD(OVERLOAD(app, A, B), Repr(B) FARGS(Repr(Arr(A, B)), Repr(A)))
INTERFACE(Lang);

/* Writing terms against any interpreter L, a `const Dyn(Lang) *`. LAM and
   APP pick the instance from the type of the body or function they are
   given, as type inference would. That argument is also read, unevaluated,
   for its type, and MSVC warns of a compound literal there, so make the
   body's environment first. */
#define LIT(L, n)      lit(Dyn(Lang))(L, n)
#define ADD(L, a, b)   add(Dyn(Lang))(L, a, b)
#define LAM(L, f)      _Generic((f) ARROWS(LAM_AT_))(L, f)
#define APP(L, f, x)   _Generic((f) ARROWS(APP_AT_))(L, f, x)
#define LAM_AT_(A, B)  , Body(A, B): lam(A, B, Dyn(Lang))
#define APP_AT_(A, B)  , Repr(Arr(A, B)): app(A, B, Dyn(Lang))


/* -------------------------------------------------------------------------- */
/* Interpreters: evaluation                                                   */
/* -------------------------------------------------------------------------- */

/* An Int is an int, and a function is the body lam was given: lam keeps it
   and app calls it. */
typedef struct { int steps; } Eval;     /* applications evaluated */

Repr(Int) lit(Eval)(const Eval *self, int n) {
    (void)self;
    return (Repr(Int)){ NEW(int, n) };
}
Repr(Int) add(Eval)(const Eval *self, Repr(Int) a, Repr(Int) b) {
    (void)self;
    return (Repr(Int)){ NEW(int, *(int *)a.p + *(int *)b.p) };
}

#define EVAL_ARROW(A, B)                                                      \
    static Repr(Arr(A, B)) lam(A, B, Eval)(const Eval *self, Body(A, B) f) {  \
        (void)self;                                                           \
        return (Repr(Arr(A, B))){ copy(&f, sizeof f) };                       \
    }                                                                         \
    static Repr(B) app(A, B, Eval)(Eval *self, Repr(Arr(A, B)) f,             \
                                   Repr(A) x) {                               \
        self->steps++;                                                        \
        return apply(Body(A, B))(f.p, x);                                     \
    }
ARROWS(EVAL_ARROW)

IMPL(Lang, Eval);


/* -------------------------------------------------------------------------- */
/* Interpreters: printing                                                     */
/* -------------------------------------------------------------------------- */

/* A term's text, and how tightly its outermost form binds, so enclosing
   forms add only the parentheses they need. lam calls the body at once, on
   a variable named for how many lambdas enclose it. */
typedef struct { int depth; } Show;     /* lambdas around the current term */
typedef struct { int prec; const char *text; } Doc;
enum { LAM_PREC, ADD_PREC, APP_PREC, ATOM_PREC };

/* printf to a new string on the heap. */
static char *fmt(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    char *s = alloc((size_t)n + 1);
    va_start(ap, format);
    vsnprintf(s, (size_t)n + 1, format, ap);
    va_end(ap);
    return s;
}

static Doc *doc(int prec, const char *text) {
    return NEW(Doc, prec, text);
}

/* The text of a Doc, parenthesized if it binds less tightly than prec. */
static const char *text_at(int prec, const Doc *d) {
    return d->prec < prec ? fmt("(%s)", d->text) : d->text;
}

Repr(Int) lit(Show)(const Show *self, int n) {
    (void)self;
    return (Repr(Int)){ doc(ATOM_PREC, fmt("%d", n)) };
}
Repr(Int) add(Show)(const Show *self, Repr(Int) a, Repr(Int) b) {
    (void)self;
    return (Repr(Int)){ doc(ADD_PREC, fmt("%s + %s", text_at(ADD_PREC, a.p),
                                                     text_at(APP_PREC, b.p))) };
}

#define SHOW_ARROW(A, B)                                                      \
    static Repr(Arr(A, B)) lam(A, B, Show)(Show *self, Body(A, B) f) {        \
        int i = self->depth++;                                                \
        Repr(A) x = { doc(ATOM_PREC, fmt("x%d", i)) };                        \
        Repr(B) body = CALL(f, x);                                            \
        self->depth--;                                                        \
        return (Repr(Arr(A, B))){                                             \
            doc(LAM_PREC, fmt("\\x%d. %s", i, text_at(LAM_PREC, body.p))) };  \
    }                                                                         \
    static Repr(B) app(A, B, Show)(const Show *self, Repr(Arr(A, B)) f,       \
                                   Repr(A) x) {                               \
        (void)self;                                                           \
        return (Repr(B)){ doc(APP_PREC, fmt("%s %s", text_at(APP_PREC, f.p),  \
                                                     text_at(ATOM_PREC, x.p))) }; \
    }
ARROWS(SHOW_ARROW)

IMPL(Lang, Show);


/* -------------------------------------------------------------------------- */
/* Examples: terms                                                            */
/* -------------------------------------------------------------------------- */

/* A lambda's body is a closure: a struct of the variables it captures, with
   an apply method. Eval keeps bodies past the call making them, so they are
   on the heap. Terms see only Dyn(Lang), never an interpreter. */

/* \x. x + x */
typedef struct { const Dyn(Lang) *L; } Double;

Repr(Int) apply(Double)(const Double *self, Repr(Int) x) {
    return ADD(self->L, x, x);
}
DELEGATE(Double, METHOD(apply, Repr(Int) FARGS(Repr(Int))));

static Repr(Arr(Int, Int)) dbl(const Dyn(Lang) *L) {
    Double *body = NEW(Double, L);
    return LAM(L, FN(Double, apply, body));
}

/* \x. \y. x + y */
typedef struct { const Dyn(Lang) *L; Repr(Int) x; } PlusX;     /* \y. x + y */
typedef struct { const Dyn(Lang) *L; } Plus;

Repr(Int) apply(PlusX)(const PlusX *self, Repr(Int) y) {
    return ADD(self->L, self->x, y);
}
DELEGATE(PlusX, METHOD(apply, Repr(Int) FARGS(Repr(Int))));

Repr(Arr(Int, Int)) apply(Plus)(const Plus *self, Repr(Int) x) {
    PlusX *body = NEW(PlusX, self->L, x);
    return LAM(self->L, FN(PlusX, apply, body));
}
DELEGATE(Plus, METHOD(apply, Repr(Arr(Int, Int)) FARGS(Repr(Int))));

static Repr(Arr(Int, Arr(Int, Int))) plus(const Dyn(Lang) *L) {
    Plus *body = NEW(Plus, L);
    return LAM(L, FN(Plus, apply, body));
}

/* \f. \x. f (f x) */
typedef struct { const Dyn(Lang) *L; Repr(Arr(Int, Int)) f; } TwiceF;
typedef struct { const Dyn(Lang) *L; } Twice;

Repr(Int) apply(TwiceF)(const TwiceF *self, Repr(Int) x) {
    return APP(self->L, self->f, APP(self->L, self->f, x));
}
DELEGATE(TwiceF, METHOD(apply, Repr(Int) FARGS(Repr(Int))));

Repr(Arr(Int, Int)) apply(Twice)(const Twice *self, Repr(Arr(Int, Int)) f) {
    TwiceF *body = NEW(TwiceF, self->L, f);
    return LAM(self->L, FN(TwiceF, apply, body));
}
DELEGATE(Twice, METHOD(apply, Repr(Arr(Int, Int)) FARGS(Repr(Arr(Int, Int)))));

static Repr(Arr(Arr(Int, Int), Arr(Int, Int))) twice(const Dyn(Lang) *L) {
    Twice *body = NEW(Twice, L);
    return LAM(L, FN(Twice, apply, body));
}

/* Programs: closed terms of type Int. An ill-typed one doesn't compile:
 *
 *   APP(L, LIT(L, 1), LIT(L, 2))    an Int is not a function
 *   APP(L, dbl(L), dbl(L))          dbl takes an Int, not an Int -> Int
 *   ADD(L, dbl(L), LIT(L, 1))       Int -> Int is not an Int
 */
typedef Repr(Int) Program(const Dyn(Lang) *L);

static Repr(Int) double_21(const Dyn(Lang) *L) {
    return APP(L, dbl(L), LIT(L, 21));
}
static Repr(Int) plus_1_2(const Dyn(Lang) *L) {
    return APP(L, APP(L, plus(L), LIT(L, 1)), LIT(L, 2));
}
static Repr(Int) twice_double_5(const Dyn(Lang) *L) {
    return APP(L, APP(L, twice(L), dbl(L)), LIT(L, 5));
}
static Repr(Int) twice_plus_10_1(const Dyn(Lang) *L) {
    return APP(L, APP(L, twice(L), APP(L, plus(L), LIT(L, 10))), LIT(L, 1));
}


/* -------------------------------------------------------------------------- */
/* Examples: running them                                                     */
/* -------------------------------------------------------------------------- */

int main(void) {
    Program *programs[] = {
        double_21, plus_1_2, twice_double_5, twice_plus_10_1,
    };
    for (size_t i = 0; i < sizeof programs / sizeof *programs; i++) {
        /* The same compiled program, interpreted two ways. */
        Show show = { 0 };
        Eval eval = { 0 };
        Dyn(Lang) by_show = DYN(Lang, Show, &show);
        Dyn(Lang) by_eval = DYN(Lang, Eval, &eval);

        const char *text = text_at(LAM_PREC, programs[i](&by_show).p);
        int value = *(int *)programs[i](&by_eval).p;
        printf("%-50s = %-3d in %d step%s\n", text, value, eval.steps,
               eval.steps == 1 ? "" : "s");
    }
    return 0;
}
