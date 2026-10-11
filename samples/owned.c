/*
 * A region that owns shapes and labels through owned.h. The region is
 * written once against IOwned and knows nothing of shapes or labels;
 * they know nothing of regions beyond leading to IOwned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "owned.h"

typedef double Dbl;

#define area(T)  OVERLOAD(area, T)
#define show(T)  OVERLOAD(show, T)

/* Two unrelated interfaces, both leading to IOwned. */
#define IShape_METHODS  METHOD(area, Dbl FARGS()), AsOwned_METHODS
INTERFACE(IShape);

#define IText_METHODS   METHOD(show, void FARGS()), AsOwned_METHODS
INTERFACE(IText);


/* -------------------------------------------------------------------------- */
/* A region, written once against IOwned                                      */
/* -------------------------------------------------------------------------- */

/* Objects of any type, moved into one block and dropped together. The
   block is the caller's, aligned for any type as malloc's is; the region
   never allocates. */
typedef struct {
    unsigned char *block;
    size_t used, cap;
    IOwned objects[16];
    size_t n;
} Region;

/* Moves o's object into the region and returns where it now lives, or
   NULL if it doesn't fit. After a move, the old object must not be used or
   dropped. */
static void *region_adopt(Region *r, IOwned o) {
    size_t size = size_of(IOwned)(&o);
    size_t align = align_of(IOwned)(&o);
    size_t at = (r->used + align - 1) / align * align;
    if (at + size > r->cap || r->n == sizeof r->objects / sizeof *r->objects)
        return NULL;
    memcpy(r->block + at, o.self, size);
    r->objects[r->n++] = (IOwned){ r->block + at, o.vt };
    r->used = at + size;
    return r->block + at;
}

/* Drops every object, newest first, and empties the region. */
static void region_drop(Region *r) {
    while (r->n > 0)
        drop(IOwned)(&r->objects[--r->n]);
    r->used = 0;
}


/* -------------------------------------------------------------------------- */
/* Types, which know IShape and IText but not regions                         */
/* -------------------------------------------------------------------------- */

typedef struct { Dbl r; } Circle;                    /* size 8, align 8 */
typedef struct { float side; } Square;               /* size 4, align 4 */
typedef struct { Dbl x, y; } Point;
typedef struct { size_t n; Point *pts; } Polygon;    /* owns its points */
typedef struct { char *chars; } Label;               /* owns its text */

Dbl area(Circle)(const Circle *self) {
    return 3.141592653589793 * self->r * self->r;
}
Dbl area(Square)(const Square *self) {
    return self->side * self->side;
}
Dbl area(Polygon)(const Polygon *self) {             /* shoelace formula */
    Dbl a = 0;
    for (size_t i = 0; i < self->n; i++) {
        Point p = self->pts[i], q = self->pts[(i + 1) % self->n];
        a += p.x * q.y - q.x * p.y;
    }
    return a < 0 ? -a / 2 : a / 2;
}
void drop(Polygon)(Polygon *self) {
    printf("  polygon: freeing its %zu points\n", self->n);
    free(self->pts);
}

void show(Label)(const Label *self) {
    printf("%s", self->chars);
}
void drop(Label)(Label *self) {
    printf("  label: freeing \"%s\"\n", self->chars);
    free(self->chars);
}

OWNED_PLAIN(Circle);
OWNED_PLAIN(Square);
OWNED(Polygon);
OWNED(Label);

IMPL(IShape, Circle);
IMPL(IShape, Square);
IMPL(IShape, Polygon);
IMPL(IText, Label);


/* -------------------------------------------------------------------------- */
/* A drawing, which keeps everything in its region                            */
/* -------------------------------------------------------------------------- */

typedef struct {
    Region region;
    IShape shapes[8];
    size_t nshapes;
    IText title;
} Drawing;

/* Each moves an object into the drawing, which owns it from then on. The
   sample's block is sized for what it adds; a real one would report a full
   region instead of stopping. */
static void add_shape(Drawing *d, IShape s) {
    void *at = d->nshapes < 8
             ? region_adopt(&d->region, as_owned(IShape)(&s)) : NULL;
    if (!at) abort();
    d->shapes[d->nshapes++] = (IShape){ at, s.vt };
}
static void set_title(Drawing *d, IText t) {
    void *at = region_adopt(&d->region, as_owned(IText)(&t));
    if (!at) abort();
    d->title = (IText){ at, t.vt };
}

static char *copy_string(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(malloc(n), s, n);
}

/* The objects start as locals here, gone when this returns; the drawing's
   moved copies live on. */
static void build(Drawing *d) {
    Circle c = { 1 };
    Square q = { 2 };
    Point *pts = malloc(3 * sizeof *pts);
    pts[0] = (Point){ 0, 0 };
    pts[1] = (Point){ 4, 0 };
    pts[2] = (Point){ 0, 3 };
    Polygon p = { 3, pts };
    Label l = { copy_string("three shapes") };

    add_shape(d, DYN(IShape, Circle, &c));
    add_shape(d, DYN(IShape, Square, &q));
    add_shape(d, DYN(IShape, Polygon, &p));    /* the drawing owns pts now */
    set_title(d, DYN(IText, Label, &l));       /* and l's text */
}

int main(void) {
    unsigned char *block = malloc(256);       /* storage, chosen by its owner */
    Drawing d = { .region = { .block = block, .cap = 256 } };
    build(&d);

    show(IText)(&d.title);
    printf(":\n");
    for (size_t i = 0; i < d.nshapes; i++)
        printf("  area %-8g at offset %td\n", area(IShape)(&d.shapes[i]),
               (unsigned char *)d.shapes[i].self - block);
    printf("dropping:\n");
    region_drop(&d.region);
    free(block);
    return 0;
}
