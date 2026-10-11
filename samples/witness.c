/*
 * witness.c: a hand-written prototype of what witness-table macros would
 * generate, so generic code can be compiled once instead of once per type.
 *
 *   Witness            what erased code needs to hold a T: size, copy, drop
 *   Vtable(I)          a conformance of a type to I: its witness, then methods
 *   generic functions  take tables for their type parameters, values by pointer
 *   generic types      have tables instantiated at run time from their
 *                      parameters' tables, cached
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monomorph.h"

#define print(T)    OVERLOAD(print, T)
#define compare(T)  OVERLOAD(compare, T)
#define empty(T)    OVERLOAD(empty, T)
#define append(T)   OVERLOAD(append, T)
#define clone(T)    OVERLOAD(clone, T)
#define drop(T)     OVERLOAD(drop, T)


/* -------------------------------------------------------------------------- */
/* Value witnesses                                                            */
/* -------------------------------------------------------------------------- */

/* Every witness function takes its own table first, so a table made at run
   time for a generic type can find its parameters' tables (see Vec). Values
   move by memcpy; copy makes an independent value in uninitialised dst. */
typedef struct Witness Witness;
struct Witness {
    const char *name;                     /* mangled name: identity across TUs */
    size_t size, align;
    void (*copy)(const Witness *w, void *dst, const void *src);
    void (*drop)(const Witness *w, void *p);
};

#define WITNESS(T)  (&MONOMORPH_MANGLE(witness, T))

/* Calls slot m of table w, passing w first. The slot is parenthesized, as
   in dyn.h's dispatchers, so a method-name macro like print(T) can't
   capture the call. */
#define VCALL(w, m, ...)  (((w)->m)((w), __VA_ARGS__))

static void copy_bits(const Witness *w, void *dst, const void *src) {
    memcpy(dst, src, w->size);
}
static void drop_nothing(const Witness *w, void *p) {
    (void)w, (void)p;
}

/* For plain data. */
#define TRIVIAL_WITNESS(T)                                                    \
    static const Witness MONOMORPH_MANGLE(witness, T) = {                     \
        MONOMORPH_STRING(T), sizeof(T), _Alignof(T), copy_bits, drop_nothing  \
    }

/* For a type with clone(T) and drop(T) methods. */
#define OWNED_WITNESS(T)                                                      \
    static void MONOMORPH_MANGLE(copy, T)(const Witness *w, void *dst,        \
                                          const void *src) {                  \
        (void)w;                                                              \
        *(T *)dst = clone(T)(src);                                            \
    }                                                                         \
    static void MONOMORPH_MANGLE(drop, Witness, T)(const Witness *w,          \
                                                   void *p) {                 \
        (void)w;                                                              \
        drop(T)(p);                                                           \
    }                                                                         \
    static const Witness MONOMORPH_MANGLE(witness, T) = {                     \
        MONOMORPH_STRING(T), sizeof(T), _Alignof(T),                          \
        MONOMORPH_MANGLE(copy, T), MONOMORPH_MANGLE(drop, Witness, T)         \
    }


/* -------------------------------------------------------------------------- */
/* Interfaces as witness tables                                               */
/* -------------------------------------------------------------------------- */

/* What INTERFACE would generate from
 *
 *   #define Print_METHODS   METHOD(print, void FARGS(Self))
 *   #define Ord_METHODS     METHOD(compare, int FARGS(Self, Self))
 *   #define Monoid_METHODS  METHOD(empty, Self FARGS()),
 *                           METHOD(append, Self FARGS(Self, Self))
 *
 * A table starts with the conforming type's witness. Each slot takes the
 * table, then Self values by pointer; a Self result is written to ret,
 * uninitialised storage of self->size bytes. */
#define Vtable(I)  MONOMORPH_TYPE(Vtable, I)
#define VTABLE(I, T)  (&MONOMORPH_MANGLE(vtable, I, T))

typedef struct Vtable(Print) Vtable(Print);
struct Vtable(Print) {
    const Witness *self;
    void (*print)(const Vtable(Print) *w, const void *x);
};

typedef struct Vtable(Ord) Vtable(Ord);
struct Vtable(Ord) {
    const Witness *self;
    int (*compare)(const Vtable(Ord) *w, const void *a, const void *b);
};

typedef struct Vtable(Monoid) Vtable(Monoid);
struct Vtable(Monoid) {
    const Witness *self;
    void (*empty)(const Vtable(Monoid) *w, void *ret);
    void (*append)(const Vtable(Monoid) *w, void *ret, const void *a,
                   const void *b);
};

/* What IMPL would generate: a thunk per method, from the concrete method
   (Self as `const T *`, a Self result returned by value), and the table. */
#define IMPL_PRINT(T)                                                         \
    static void MONOMORPH_MANGLE(print, Print, T)(const Vtable(Print) *w,     \
                                                  const void *x) {            \
        (void)w;                                                              \
        print(T)(x);                                                          \
    }                                                                         \
    static const Vtable(Print) MONOMORPH_MANGLE(vtable, Print, T) = {         \
        WITNESS(T), MONOMORPH_MANGLE(print, Print, T)                         \
    }

#define IMPL_ORD(T)                                                           \
    static int MONOMORPH_MANGLE(compare, Ord, T)(const Vtable(Ord) *w,        \
                                                 const void *a,               \
                                                 const void *b) {             \
        (void)w;                                                              \
        return compare(T)(a, b);                                              \
    }                                                                         \
    static const Vtable(Ord) MONOMORPH_MANGLE(vtable, Ord, T) = {             \
        WITNESS(T), MONOMORPH_MANGLE(compare, Ord, T)                         \
    }

#define IMPL_MONOID(T)                                                        \
    static void MONOMORPH_MANGLE(empty, Monoid, T)(const Vtable(Monoid) *w,   \
                                                   void *ret) {               \
        (void)w;                                                              \
        *(T *)ret = empty(T)();                                               \
    }                                                                         \
    static void MONOMORPH_MANGLE(append, Monoid, T)(const Vtable(Monoid) *w,  \
                                                    void *ret, const void *a, \
                                                    const void *b) {          \
        (void)w;                                                              \
        *(T *)ret = append(T)(a, b);                                          \
    }                                                                         \
    static const Vtable(Monoid) MONOMORPH_MANGLE(vtable, Monoid, T) = {       \
        WITNESS(T), MONOMORPH_MANGLE(empty, Monoid, T),                       \
        MONOMORPH_MANGLE(append, Monoid, T)                                   \
    }


/* -------------------------------------------------------------------------- */
/* Concrete types: ordinary monomorphic methods                               */
/* -------------------------------------------------------------------------- */

void print(int)(const int *x) { printf("%d", *x); }
int compare(int)(const int *a, const int *b) { return (*a > *b) - (*a < *b); }
int empty(int)(void) { return 0; }
int append(int)(const int *a, const int *b) { return *a + *b; }

TRIVIAL_WITNESS(int);
IMPL_PRINT(int);
IMPL_ORD(int);
IMPL_MONOID(int);

/* An owned string. */
typedef struct { char *chars; } Str;

static Str str(const char *s) {
    size_t n = strlen(s) + 1;
    return (Str){ memcpy(malloc(n), s, n) };
}

Str clone(Str)(const Str *s) { return str(s->chars); }
void drop(Str)(Str *s) { free(s->chars); }
void print(Str)(const Str *s) { printf("\"%s\"", s->chars); }
int compare(Str)(const Str *a, const Str *b) {
    return strcmp(a->chars, b->chars);
}
Str empty(Str)(void) { return str(""); }
Str append(Str)(const Str *a, const Str *b) {
    size_t m = strlen(a->chars), n = strlen(b->chars);
    char *s = malloc(m + n + 1);
    memcpy(s, a->chars, m);
    memcpy(s + m, b->chars, n + 1);
    return (Str){ s };
}

OWNED_WITNESS(Str);
IMPL_PRINT(Str);
IMPL_ORD(Str);
IMPL_MONOID(Str);


/* -------------------------------------------------------------------------- */
/* Generic functions, compiled once                                           */
/* -------------------------------------------------------------------------- */

/* ∀T. Ord T ⇒ sort(T xs[n]). A constrained type parameter is passed as its
   table, which carries T's witness; values are untyped bytes at stride
   T's size. C11 has no portable alloca and VLAs are optional, so a
   temporary of unknown size goes on the heap. */
static void sort(const Vtable(Ord) *ord, void *xs, size_t n) {
    size_t size = ord->self->size;
    char *at = xs;
    void *x = malloc(size);
    for (size_t i = 1; i < n; i++) {
        memcpy(x, at + i * size, size);
        size_t j = i;
        for (; j > 0 && VCALL(ord, compare, at + (j - 1) * size, x) > 0; j--)
            memcpy(at + j * size, at + (j - 1) * size, size);
        memcpy(at + j * size, x, size);
    }
    free(x);
}

/* ∀T. Monoid T ⇒ mconcat(T xs[n]) -> T, written to ret. */
static void mconcat(const Vtable(Monoid) *m, void *ret, const void *xs,
                    size_t n) {
    const Witness *t = m->self;
    const char *at = xs;
    void *acc = malloc(t->size), *next = malloc(t->size);
    VCALL(m, empty, acc);
    for (size_t i = 0; i < n; i++) {
        VCALL(m, append, next, acc, at + i * t->size);
        VCALL(t, drop, acc);
        void *done = acc;
        acc = next;
        next = done;
    }
    memcpy(ret, acc, t->size);
    free(acc);
    free(next);
}


/* -------------------------------------------------------------------------- */
/* A generic type, with tables instantiated at run time                       */
/* -------------------------------------------------------------------------- */

/* Vec T for any T: elements at stride T's size. */
typedef struct { void *items; size_t n; } Vec;

/* Vec T's witness extends Witness with T's; its functions find it there. */
typedef struct { Witness base; const Witness *t; } VecWitness;

static void copy_vec(const Witness *w, void *dst, const void *src) {
    const Witness *t = ((const VecWitness *)w)->t;
    const Vec *v = src;
    Vec u = { malloc(v->n * t->size), v->n };
    for (size_t i = 0; i < v->n; i++)
        VCALL(t, copy, (char *)u.items + i * t->size,
                (const char *)v->items + i * t->size);
    *(Vec *)dst = u;
}
static void drop_vec(const Witness *w, void *p) {
    const Witness *t = ((const VecWitness *)w)->t;
    Vec *v = p;
    for (size_t i = 0; i < v->n; i++)
        VCALL(t, drop, (char *)v->items + i * t->size);
    free(v->items);
}

/* Print T ⇒ Print (Vec T), a conditional conformance: the table holds the
   one it was made from. */
typedef struct { Vtable(Print) base; const Vtable(Print) *t; } VecPrint;

static void print_vec(const Vtable(Print) *w, const void *x) {
    const Vtable(Print) *t = ((const VecPrint *)w)->t;
    const Vec *v = x;
    printf("[");
    for (size_t i = 0; i < v->n; i++) {
        if (i) printf(", ");
        VCALL(t, print, (const char *)v->items + i * t->self->size);
    }
    printf("]");
}

/* Instantiation: one table per argument table, kept for the program's life
   and found again by identity, as Swift caches generic metadata. A real
   runtime needs a lock or per-thread caches. */
static const Witness *vec_witness(const Witness *t) {
    static struct Cache { const Witness *t; VecWitness w; struct Cache *next; }
        *cache;
    for (struct Cache *c = cache; c; c = c->next)
        if (c->t == t) return &c->w.base;
    struct Cache *c = malloc(sizeof *c);
    size_t n = strlen(t->name) + sizeof "Vecǀǀ";
    char *name = malloc(n);
    snprintf(name, n, "Vecǀ%sǀ", t->name);
    *c = (struct Cache){ t, { { name, sizeof(Vec), _Alignof(Vec),
                                copy_vec, drop_vec }, t }, cache };
    cache = c;
    return &c->w.base;
}

static const Vtable(Print) *vec_print(const Vtable(Print) *t) {
    static struct Cache { const Vtable(Print) *t; VecPrint p;
                          struct Cache *next; } *cache;
    for (struct Cache *c = cache; c; c = c->next)
        if (c->t == t) return &c->p.base;
    struct Cache *c = malloc(sizeof *c);
    *c = (struct Cache){ t, { { vec_witness(t->self), print_vec }, t }, cache };
    cache = c;
    return &c->p.base;
}


/* -------------------------------------------------------------------------- */
/* Polymorphic recursion                                                      */
/* -------------------------------------------------------------------------- */

/* ∀T. Print T ⇒ nest(n, x) prints x paired with itself n levels deep. It
   calls itself at Vec T, so monomorphisation would need an instance per
   depth, without end; erased, it is one function, instantiating Vec T's
   tables as it goes. */
static void nest(const Vtable(Print) *p, int n, const void *x) {
    if (n == 0) {
        VCALL(p, print, x);
        return;
    }
    const Witness *t = p->self;
    Vec pair = { malloc(2 * t->size), 2 };
    VCALL(t, copy, pair.items, x);
    VCALL(t, copy, (char *)pair.items + t->size, x);
    const Vtable(Print) *pv = vec_print(p);
    nest(pv, n - 1, &pair);
    VCALL(pv->self, drop, &pair);
}


/* -------------------------------------------------------------------------- */
/* Existentials share the tables                                              */
/* -------------------------------------------------------------------------- */

/* An IPrint, owning its value: the table's witness says how to drop it. */
typedef struct { void *self; const Vtable(Print) *vt; } IPrint;

static IPrint dyn_print(const Vtable(Print) *vt, const void *x) {
    IPrint d = { malloc(vt->self->size), vt };
    VCALL(vt->self, copy, d.self, x);
    return d;
}
static void drop_dyn_print(IPrint *d) {
    VCALL(d->vt->self, drop, d->self);
    free(d->self);
}


int main(void) {
    int ints[] = { 3, 1, 2 };
    Str strs[] = { str("pear"), str("apple"), str("fig") };

    /* One compiled sort, two element types. */
    sort(VTABLE(Ord, int), ints, 3);
    sort(VTABLE(Ord, Str), strs, 3);
    for (int i = 0; i < 3; i++) printf("%d ", ints[i]);
    for (int i = 0; i < 3; i++) printf("%s ", strs[i].chars);
    printf("\n");

    /* One compiled mconcat; Self comes back through ret. */
    int sum;
    Str cat;
    mconcat(VTABLE(Monoid, int), &sum, ints, 3);
    mconcat(VTABLE(Monoid, Str), &cat, strs, 3);
    printf("%d %s\n", sum, cat.chars);
    drop(Str)(&cat);

    /* Polymorphic recursion. */
    nest(VTABLE(Print, int), 3, &ints[0]);
    printf("\n");
    nest(VTABLE(Print, Str), 2, &strs[0]);
    printf("\n");
    printf("%s\n", vec_print(vec_print(VTABLE(Print, int)))->self->name);

    /* Existentials, of three types, built from the same tables. */
    Vec v = { ints, 3 };
    IPrint xs[] = {
        dyn_print(VTABLE(Print, int), &ints[2]),
        dyn_print(VTABLE(Print, Str), &strs[1]),
        dyn_print(vec_print(VTABLE(Print, int)), &v),
    };
    for (int i = 0; i < 3; i++) {
        VCALL(xs[i].vt, print, xs[i].self);
        printf(i < 2 ? " " : "\n");
        drop_dyn_print(&xs[i]);
    }

    for (int i = 0; i < 3; i++) drop(Str)(&strs[i]);
    return 0;
}
