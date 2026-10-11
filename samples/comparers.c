/*
 * Generic interfaces, in the style of C#'s IComparer<T>: a comparer is an
 * object of its own, passed to generic code beside the values it works on.
 * Comparers can carry state and be chosen at run time, a type can have
 * several, and a method like a monoid's empty needs no value at all. The
 * same interfaces, instantiated at an erased element type, also serve code
 * compiled once for values whose type is known only at run time.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dyn.h"

typedef const char *Str;

#define compare(T)  OVERLOAD(compare, T)
#define show(T)     OVERLOAD(show, T)
#define empty(T)    OVERLOAD(empty, T)
#define combine(T)  OVERLOAD(combine, T)
#define sort(T)     OVERLOAD(sort, T)
#define fold(T)     OVERLOAD(fold, T)


/* -------------------------------------------------------------------------- */
/* Generic interfaces                                                         */
/* -------------------------------------------------------------------------- */

/* Each is named by a type constructor, with a method list taking the same
   argument. INTERFACE_OF instantiates one. */
#define IComparer(T)  TYPE(IComparer, T)            /* orders two Ts    */
#define IComparer_METHODS(T)                                                  \
    METHOD(compare, int FARGS(const T *, const T *))

#define IShow(T)  TYPE(IShow, T)                    /* prints a T       */
#define IShow_METHODS(T)                                                      \
    METHOD(show, void FARGS(const T *))

#define IMonoid(T)  TYPE(IMonoid, T)                /* empty takes no T */
#define IMonoid_METHODS(T)                                                    \
    METHOD(empty,   T FARGS()),                                               \
    METHOD(combine, T FARGS(T, T))

INTERFACE_OF(IComparer, int);
INTERFACE_OF(IComparer, Str);
INTERFACE_OF(IShow, int);
INTERFACE_OF(IShow, Str);
INTERFACE_OF(IMonoid, int);


/* -------------------------------------------------------------------------- */
/* Generic code over a type known at compile time                             */
/* -------------------------------------------------------------------------- */

/* sort(T) and fold(T) are instantiated per T, as monomorph.h does, so C
   checks every operand. The comparer or monoid is a value, chosen at run
   time. */
#define DEFINE_SORT(T)                                                        \
    static void sort(T)(T *xs, size_t n, const IComparer(T) *by) {            \
        for (size_t i = 1; i < n; i++) {                                      \
            T x = xs[i];                                                      \
            size_t j = i;                                                     \
            for (; j > 0 && compare(IComparer(T))(by, &xs[j - 1], &x) > 0;    \
                 j--)                                                         \
                xs[j] = xs[j - 1];                                            \
            xs[j] = x;                                                        \
        }                                                                     \
    }
DEFINE_SORT(int)
DEFINE_SORT(Str)

#define DEFINE_FOLD(T)                                                        \
    static T fold(T)(const T *xs, size_t n, const IMonoid(T) *m) {            \
        T acc = empty(IMonoid(T))(m);                                         \
        for (size_t i = 0; i < n; i++)                                        \
            acc = combine(IMonoid(T))(m, acc, xs[i]);                         \
        return acc;                                                           \
    }
DEFINE_FOLD(int)


/* -------------------------------------------------------------------------- */
/* Comparers, printers and monoids                                            */
/* -------------------------------------------------------------------------- */

/* One without state is an incomplete type, used through a null pointer. */
typedef struct Ascending Ascending;
int compare(Ascending)(const Ascending *self, const int *a, const int *b) {
    (void)self;
    return (*a > *b) - (*a < *b);
}
IMPL_OF(IComparer, (int), Ascending);

/* One with state: alphabetical, optionally ignoring case. */
typedef struct { bool ignore_case; } Alphabetical;
int compare(Alphabetical)(const Alphabetical *self, const Str *a,
                          const Str *b) {
    if (!self->ignore_case)
        return strcmp(*a, *b);
    for (const char *p = *a, *q = *b;; p++, q++) {
        int d = tolower((unsigned char)*p) - tolower((unsigned char)*q);
        if (d != 0 || *p == '\0')
            return d;
    }
}
IMPL_OF(IComparer, (Str), Alphabetical);

/* A generic one, holding another: the reverse of its order. */
#define Reverse(T)  TYPE(Reverse, T)
#define DEFINE_REVERSE(T)                                                     \
    typedef struct { IComparer(T) by; } Reverse(T);                           \
    int compare(Reverse(T))(const Reverse(T) *self, const T *a, const T *b) { \
        return compare(IComparer(T))(&self->by, b, a);                        \
    }                                                                         \
    IMPL_OF(IComparer, (T), Reverse(T));
DEFINE_REVERSE(int)
DEFINE_REVERSE(Str)

typedef struct Decimal Decimal;
void show(Decimal)(const Decimal *self, const int *x) {
    (void)self;
    printf("%d", *x);
}
IMPL_OF(IShow, (int), Decimal);

typedef struct Quoted Quoted;
void show(Quoted)(const Quoted *self, const Str *s) {
    (void)self;
    printf("\"%s\"", *s);
}
IMPL_OF(IShow, (Str), Quoted);

/* Two monoids for one type, which a type class can't have, since each is a
   value of its own. empty is an ordinary method of the monoid. */
typedef struct Sum Sum;
typedef struct Product Product;

int empty(Sum)(const Sum *self) {
    (void)self;
    return 0;
}
int combine(Sum)(const Sum *self, int a, int b) {
    (void)self;
    return a + b;
}
int empty(Product)(const Product *self) {
    (void)self;
    return 1;
}
int combine(Product)(const Product *self, int a, int b) {
    (void)self;
    return a * b;
}
IMPL_OF(IMonoid, (int), Sum);
IMPL_OF(IMonoid, (int), Product);


/* -------------------------------------------------------------------------- */
/* The same interfaces, for values whose type is known only at run time       */
/* -------------------------------------------------------------------------- */

/* Any is void, so an IComparer(Any) compares two `const void *`, and code
   using it is compiled once for every element type. Erased(T) implements
   the Any instances by holding typed ones, so any comparer or printer of
   a T serves. */
typedef void Any;
INTERFACE_OF(IComparer, Any);
INTERFACE_OF(IShow, Any);

#define Erased(T)  TYPE(Erased, T)
#define DEFINE_ERASED(T)                                                      \
    typedef struct { IComparer(T) by; IShow(T) as; } Erased(T);               \
    int compare(Erased(T))(const Erased(T) *self, const Any *a,               \
                           const Any *b) {                                    \
        return compare(IComparer(T))(&self->by, a, b);                        \
    }                                                                         \
    void show(Erased(T))(const Erased(T) *self, const Any *x) {               \
        show(IShow(T))(&self->as, x);                                         \
    }                                                                         \
    IMPL_OF(IComparer, (Any), Erased(T));                                     \
    IMPL_OF(IShow, (Any), Erased(T));
DEFINE_ERASED(int)
DEFINE_ERASED(Str)

/* A column: values of one type, chosen at run time, at stride size, with
   that type's comparer and printer. */
typedef struct {
    const char *name;
    void *items;
    size_t n, size;
    IComparer(Any) by;
    IShow(Any) as;
} Column;

static void *at(const Column *c, size_t i) {
    return (char *)c->items + i * c->size;
}

/* Compiled once, for every column type. Each comparison is between two
   elements of one column, through its one comparer, so this binary method
   needs no check that its operands are the same type. */
static void sort_column(Column *c) {
    void *x = malloc(c->size);
    for (size_t i = 1; i < c->n; i++) {
        memcpy(x, at(c, i), c->size);
        size_t j = i;
        for (; j > 0 && compare(IComparer(Any))(&c->by, at(c, j - 1), x) > 0;
             j--)
            memcpy(at(c, j), at(c, j - 1), c->size);
        memcpy(at(c, j), x, c->size);
    }
    free(x);
}

static void print_column(const Column *c) {
    printf("  %-6s", c->name);
    for (size_t i = 0; i < c->n; i++) {
        printf(" ");
        show(IShow(Any))(&c->as, at(c, i));
    }
    printf("\n");
}

static void *copy(const void *p, size_t n) {
    return memcpy(malloc(n), p, n);
}


int main(void) {
    int counts[] = { 3, 1, 4, 2 };
    Str fruits[] = { "pear", "apple", "Fig" };

    /* Comparers and monoids are values, chosen at run time. */
    IComparer(int) up = DYN(IComparer(int), Ascending, NULL);
    Reverse(int) reversed = { up };
    IComparer(int) down = DYN(IComparer(int), Reverse(int), &reversed);
    Alphabetical exact = { false }, folded = { true };
    IComparer(Str) by_exact = DYN(IComparer(Str), Alphabetical, &exact);
    IComparer(Str) by_folded = DYN(IComparer(Str), Alphabetical, &folded);
    IMonoid(int) sum = DYN(IMonoid(int), Sum, NULL);
    IMonoid(int) product = DYN(IMonoid(int), Product, NULL);

    /* One sort per type, in any order. */
    int xs[4];
    memcpy(xs, counts, sizeof xs);
    sort(int)(xs, 4, &down);
    printf("descending:  %d %d %d %d\n", xs[0], xs[1], xs[2], xs[3]);

    Str ws[3] = { fruits[0], fruits[1], fruits[2] };
    sort(Str)(ws, 3, &by_exact);
    printf("exact:       %s %s %s\n", ws[0], ws[1], ws[2]);
    sort(Str)(ws, 3, &by_folded);
    printf("ignore case: %s %s %s\n", ws[0], ws[1], ws[2]);

    /* Two monoids for int. empty needs no value, so neither does folding
       nothing. */
    printf("sum %d, product %d, product of none %d\n",
           fold(int)(counts, 4, &sum), fold(int)(counts, 4, &product),
           fold(int)(counts, 0, &product));

    /* Columns whose types come from a schema, as if read from a file. */
    Erased(int) ints = { down, DYN(IShow(int), Decimal, NULL) };
    Erased(Str) strs = { by_folded, DYN(IShow(Str), Quoted, NULL) };
    struct { const char *name, *type; } schema[] = {
        { "count", "int" }, { "fruit", "str" },
    };
    printf("columns, sorted:\n");
    for (size_t i = 0; i < 2; i++) {
        Column c = strcmp(schema[i].type, "int") == 0
            ? (Column){ schema[i].name, copy(counts, sizeof counts), 4,
                        sizeof(int), DYN(IComparer(Any), Erased(int), &ints),
                        DYN(IShow(Any), Erased(int), &ints) }
            : (Column){ schema[i].name, copy(fruits, sizeof fruits), 3,
                        sizeof(Str), DYN(IComparer(Any), Erased(Str), &strs),
                        DYN(IShow(Any), Erased(Str), &strs) };
        sort_column(&c);
        print_column(&c);
        free(c.items);
    }
    return 0;
}
