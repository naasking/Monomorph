#include <stddef.h>
#include <stdio.h>
#include "fn.h"

typedef double Dbl;

/* Method names: methods are ordinary overloads. */
#define apply(T)  OVERLOAD(apply, T)
#define next(T)   OVERLOAD(next, T)
#define area(T)   OVERLOAD(area, T)

FN_DEFINE(Dbl);              /* Fn(Dbl):            () -> Dbl          */
FN_DEFINE(Dbl, Dbl);         /* Fn(Dbl, Dbl):       Dbl -> Dbl         */
FN_DEFINE(Dbl, Dbl, Dbl);    /* Fn(Dbl, Dbl, Dbl):  (Dbl, Dbl) -> Dbl  */
FN_DEFINE(int);              /* Fn(int):            () -> int          */

/* fn (x, y) => w1*x + w2*y
   The environment is a plain struct; the code is a method. */
typedef struct { Dbl w1, w2; } Weighted;

Dbl apply(Weighted)(const Weighted *self, Dbl x, Dbl y) {
    return self->w1 * x + self->w2 * y;
}
DELEGATE(Fn(Dbl, Dbl, Dbl), Weighted, apply);

/* fn y => f (x, y): a struct holding the closure and the fixed argument */
typedef struct { Fn(Dbl, Dbl, Dbl) f; Dbl x; } WithX;

Dbl apply(WithX)(const WithX *self, Dbl y) {
    return CALL(self->f, self->x, y);
}
DELEGATE(Fn(Dbl, Dbl), WithX, apply);

/* A closure with mutable state. */
typedef struct { int n; } Counter;

int next(Counter)(Counter *self) { return ++self->n; }
DELEGATE(Fn(int), Counter, next);

/* Any existing method can be used as a closure, e.g. a shape's area. */
typedef struct { Dbl r; } Circle;

Dbl area(Circle)(const Circle *self) {
    return 3.141592653589793 * self->r * self->r;
}
DELEGATE(Fn(Dbl), Circle, area);

/* A higher-order function over any Fn(Dbl, Dbl). */
static Dbl sum_over(Fn(Dbl, Dbl) f, const Dbl *xs, size_t n) {
    Dbl s = 0;
    for (size_t i = 0; i < n; i++) s += CALL(f, xs[i]);
    return s;
}

int main(void) {
    Weighted w = { 0.25, 0.75 };
    Fn(Dbl, Dbl, Dbl) f = FN(Weighted, apply, &w);
    printf("%g\n", CALL(f, 4, 8));                     /* 7  */

    WithX h = { f, 4 };
    Fn(Dbl, Dbl) g = FN(WithX, apply, &h);
    Dbl xs[] = { 0, 4, 8 };
    printf("%g %g\n", CALL(g, 8), sum_over(g, xs, 3));  /* 7 12 */

    Counter k = { 0 };
    Fn(int) tick = FN(Counter, next, &k);
    int a = CALL(tick), b = CALL(tick);
    printf("%d %d %d\n", a, b, k.n);                    /* 1 2 2 */

    Circle c = { 1 };
    Fn(Dbl) area = FN(Circle, area, &c);
    printf("%g\n", CALL(area));                         /* 3.14159 */
    return 0;
}