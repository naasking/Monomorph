#include <stddef.h>
#include <stdio.h>
#include "dyn.h"

typedef double Dbl;
#define PI 3.141592653589793

/* Method names: methods are ordinary overloads. */
#define area(T)       OVERLOAD(area, T)
#define perimeter(T)  OVERLOAD(perimeter, T)
#define scale(T)      OVERLOAD(scale, T)
#define describe(T)   OVERLOAD(describe, T)

/* An interface: its methods' names and signatures. */
#define Shape_METHODS                   \
    METHOD(area,      Dbl  FARGS()),    \
    METHOD(perimeter, Dbl  FARGS()),    \
    METHOD(scale,     void FARGS(Dbl))
INTERFACE(Shape);

/* Two types that know nothing about Shape. */
typedef struct { Dbl r; } Circle;
typedef struct { Dbl w, h; } Rect;

/* Their methods: overloads, callable directly as area(Circle)(&c). */
Dbl area(Circle)(const Circle *self) {
    return PI * self->r * self->r;
}
Dbl perimeter(Circle)(const Circle *self) {
    return 2 * PI * self->r;
}
void scale(Circle)(Circle *self, Dbl k) {
    self->r *= k;
}

Dbl area(Rect)(const Rect *self) {
    return self->w * self->h;
}
Dbl perimeter(Rect)(const Rect *self) {
    return 2 * (self->w + self->h);
}
void scale(Rect)(Rect *self, Dbl k) {
    self->w *= k;
    self->h *= k;
}

/* Implementing Shape for them; this could be in any file. */
IMPL(Shape, Circle);
IMPL(Shape, Rect);

/* A second interface, added later, reusing an existing method. */
#define Sized_METHODS  METHOD(area, Dbl FARGS())
INTERFACE(Sized);
IMPL(Sized, Rect);

/* Generic code, instantiated for a concrete type or for a Dyn. */
#define DEFINE_DESCRIBE(T)                                                   \
    static void describe(T)(const T *x) {                                    \
        printf("area %6.2f   perimeter %6.2f\n", area(T)(x), perimeter(T)(x)); \
    }
DEFINE_DESCRIBE(Circle)       /* calls areaǀCircleǀ directly           */
DEFINE_DESCRIBE(Dyn(Shape))   /* calls through each object's table     */

int main(void) {
    Circle c = { 1 };
    Rect   r = { 2, 3 };

    describe(Circle)(&c);                          /* static dispatch  */

    Dyn(Shape) shapes[] = { DYN(Shape, Circle, &c), DYN(Shape, Rect, &r) };
    for (size_t i = 0; i < 2; i++) {
        scale(Dyn(Shape))(&shapes[i], 2);          /* dynamic dispatch */
        describe(Dyn(Shape))(&shapes[i]);
    }

    Dyn(Sized) z = DYN(Sized, Rect, &r);
    printf("%g %g\n", area(Circle)(&c), area(Dyn(Sized))(&z));  /* 12.5664 24 */
    return 0;
}