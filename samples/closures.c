#include <stddef.h>
#include <stdio.h>
#include "fn.h"

typedef double Dbl;

/* Method names: methods are ordinary overloads. */
#define apply(T)     OVERLOAD(apply, T)
#define next(T)      OVERLOAD(next, T)
#define area(T)      OVERLOAD(area, T)
#define sum_over(T)  OVERLOAD(sum_over, T)

/* Closure types, written like the methods they call. A short name for one
   is a macro: a typedef would be spelled differently in method names. */
#define DblFn  Fn(Dbl FARGS(Dbl))
FN_DEFINE(Dbl FARGS());
FN_DEFINE(Dbl FARGS(Dbl));
FN_DEFINE(Dbl FARGS(Dbl, Dbl));
FN_DEFINE(int FARGS());

/* fn (x, y) => w1*x + w2*y
   The environment is a plain struct; the code is a method. */
typedef struct { Dbl w1, w2; } Weighted;

Dbl apply(Weighted)(const Weighted *self, Dbl x, Dbl y) {
    return self->w1 * x + self->w2 * y;
}
DELEGATE(Weighted, METHOD(apply, Dbl FARGS(Dbl, Dbl)));

/* fn y => f (x, y): a struct holding the closure and the fixed argument */
typedef struct { Fn(Dbl FARGS(Dbl, Dbl)) f; Dbl x; } WithX;

Dbl apply(WithX)(const WithX *self, Dbl y) {
    return CALL(self->f, self->x, y);
}
DELEGATE(WithX, METHOD(apply, Dbl FARGS(Dbl)));

/* A closure with mutable state. */
typedef struct { int n; } Counter;

int next(Counter)(Counter *self) { return ++self->n; }
DELEGATE(Counter, METHOD(next, int FARGS()));

/* Any existing method can be used as a closure, e.g. a shape's area. */
typedef struct { Dbl r; } Circle;

Dbl area(Circle)(const Circle *self) {
    return 3.141592653589793 * self->r * self->r;
}
DELEGATE(Circle, METHOD(area, Dbl FARGS()));

/* Generic code over anything with apply(T), instantiated for a concrete
   type and for every closure with its signature. */
#define DEFINE_SUM_OVER(T)                                                   \
    static Dbl sum_over(T)(const T *f, const Dbl *xs, size_t n) {            \
        Dbl s = 0;                                                           \
        for (size_t i = 0; i < n; i++) s += apply(T)(f, xs[i]);              \
        return s;                                                            \
    }
DEFINE_SUM_OVER(WithX)        /* calls applyǀWithXǀ directly       */
DEFINE_SUM_OVER(DblFn)        /* calls through each closure's code */

int main(void) {
    Weighted w = { 0.25, 0.75 };
    Fn(Dbl FARGS(Dbl, Dbl)) f = FN(Weighted, apply, &w);
    printf("%g\n", CALL(f, 4, 8));                          /* 7 */

    WithX h = { f, 4 };
    DblFn g = FN(WithX, apply, &h);
    Dbl xs[] = { 0, 4, 8 };
    printf("%g %g %g\n", apply(DblFn)(&g, 8),               /* 7 12 12 */
           sum_over(WithX)(&h, xs, 3), sum_over(DblFn)(&g, xs, 3));

    Counter k = { 0 };
    Fn(int FARGS()) tick = FN(Counter, next, &k);
    int a = CALL(tick), b = CALL(tick);
    printf("%d %d %d\n", a, b, k.n);                        /* 1 2 2 */

    Circle c = { 1 };
    Fn(Dbl FARGS()) area = FN(Circle, area, &c);
    printf("%g\n", CALL(area));                             /* 3.14159 */
    return 0;
}
