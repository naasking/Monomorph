#include <stddef.h>
#include <stdio.h>
#include "fn.h"

typedef double Dbl;

/* Method names: methods are ordinary overloads. */
#define area(T)  OVERLOAD(area, T)

/* fn (x, y) => w1*x + w2*y
   The environment is a plain struct; the code takes it as a void *. */
typedef struct { Dbl w1, w2; } Weighted;

static Dbl weighted(void *env, Dbl x, Dbl y) {
    const Weighted *self = env;
    return self->w1 * x + self->w2 * y;
}

/* fn y => f (x, y): a struct holding the closure and the fixed argument */
typedef struct { Fn(Dbl, Dbl, Dbl) f; Dbl x; } WithX;

static Dbl with_x(void *env, Dbl y) {
    const WithX *self = env;
    return apply(self->f, self->x, y);
}

/* A closure with mutable state. */
typedef struct { int n; } Counter;

static int next(void *env) {
    Counter *self = env;
    return ++self->n;
}

/* An existing method, e.g. a shape's area, becomes a closure by adapter. */
typedef struct { Dbl r; } Circle;

Dbl area(Circle)(const Circle *self) {
    return 3.141592653589793 * self->r * self->r;
}
static Dbl circle_area(void *env) { return area(Circle)(env); }

/* Argument types can be any C type. */
static size_t count_char(void *env, const char *s) {
    size_t n = 0;
    for (; *s; s++) n += *s == *(const char *)env;
    return n;
}

/* A higher-order function over any Fn(Dbl, Dbl). */
static Dbl sum_over(Fn(Dbl, Dbl) f, const Dbl *xs, size_t n) {
    Dbl s = 0;
    for (size_t i = 0; i < n; i++) s += apply(f, xs[i]);
    return s;
}

int main(void) {
    Weighted w = { 0.25, 0.75 };
    Fn(Dbl, Dbl, Dbl) f = FN(weighted, &w);
    printf("%g\n", apply(f, 4, 8));                       /* 7  */

    WithX h = { f, 4 };
    Fn(Dbl, Dbl) g = FN(with_x, &h);
    Dbl xs[] = { 0, 4, 8 };
    printf("%g %g\n", apply(g, 8), sum_over(g, xs, 3));  /* 7 12 */

    Counter k = { 0 };
    Fn(int) tick = FN(next, &k);
    int a = apply(tick), b = apply(tick);
    printf("%d %d %d\n", a, b, k.n);                      /* 1 2 2 */

    Circle c = { 1 };
    Fn(Dbl) circ = FN(circle_area, &c);
    printf("%g\n", apply(circ));                          /* 3.14159 */

    char comma = ',';
    Fn(const char *, size_t) commas = FN(count_char, &comma);
    printf("%zu\n", apply(commas, "a,b,c"));              /* 2 */

    /* Closures kept in a table get records that live as long as it does. */
    Counter ks[2] = {{ 0 }, { 10 }};
    Closure(int) recs[2] = {{ next, &ks[0] }, { next, &ks[1] }};
    Fn(int) ticks[2] = { &recs[0].code, &recs[1].code };
    apply(ticks[0]);
    printf("%d %d\n", apply(ticks[0]), apply(ticks[1]));  /* 2 11 */
    return 0;
}
