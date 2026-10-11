/*
 * owned.h
 *
 * An example of what can be built on dyn.h without changing it: an
 * interface through which code holding an interface value can own the
 * object behind it, moving it into storage of its own choosing and dropping
 * it later, without knowing its type. Not one of the core headers. Nothing
 * here allocates.
 *
 * An interface value borrows: it points at an object someone else keeps
 * alive. To own that object instead, its holder needs three facts about
 * the hidden type:
 *
 *   size_of    how many bytes to move
 *   align_of   where in the holder's storage they may go
 *   drop       how to release what the object itself owns
 *
 * These make up the IOwned interface. A type opts in once:
 *
 *   OWNED_PLAIN(Circle);                        owns nothing to release
 *
 *   void drop(Polygon)(Polygon *self) { free(self->pts); }
 *   OWNED(Polygon);                             releases its points
 *
 * and any interface leads to it with one method:
 *
 *   #define IShape_METHODS  METHOD(area, Dbl FARGS()), AsOwned_METHODS
 *
 *   IOwned o = as_owned(IShape)(&s);            the same object, as IOwned
 *
 * so code written once against IOwned, such as an arena, serves every
 * interface that leads to it.
 *
 * Moving is a memcpy of size_of bytes, after which the old object must not
 * be used or dropped. Opt in only types that can be moved that way: not
 * intrusive list nodes, objects whose address is held elsewhere, or pthread
 * mutexes, and not structs with a flexible array member, which sizeof
 * leaves out. OWNED needs a complete type, so not an opaque one.
 */
#ifndef OWNED_H
#define OWNED_H

#include <stddef.h>
#include "dyn.h"

#define drop(T)      MONOMORPH_OVERLOAD(drop, T)
#define size_of(T)   MONOMORPH_OVERLOAD(size_of, T)
#define align_of(T)  MONOMORPH_OVERLOAD(align_of, T)
#define as_owned(T)  MONOMORPH_OVERLOAD(as_owned, T)


/* -------------------------------------------------------------------------- */
/* Interfaces                                                                 */
/* -------------------------------------------------------------------------- */

/* The parts, for other combinations, such as with a Clone interface. */
#define Layout_METHODS                                                        \
    METHOD(size_of,  size_t FARGS()),                                         \
    METHOD(align_of, size_t FARGS())
#define Drop_METHODS                                                          \
    METHOD(drop, void FARGS())

/* Everything a holder needs to own an object of a type it doesn't know. */
#define IOwned_METHODS  Layout_METHODS, Drop_METHODS
INTERFACE(IOwned);

/* For any interface whose objects may be owned through it:
   as_owned(I)(&i) gives the same object as an IOwned, made for its own
   type, so the two views always agree. */
#define AsOwned_METHODS  METHOD(as_owned, IOwned FARGS())


/* -------------------------------------------------------------------------- */
/* Opting in                                                                  */
/* -------------------------------------------------------------------------- */

/*
 * OWNED(T);         for a type with its own drop(T)(T *self)
 * OWNED_PLAIN(T);   for a type that owns nothing, whose drop does nothing
 *
 *   Defines size_of(T) and align_of(T) from sizeof and _Alignof, implements
 *   IOwned for T, and defines as_owned(T), so T can implement interfaces
 *   that include AsOwned_METHODS. Once per type, before those IMPLs.
 */
#define OWNED(T)                                                              \
    DYN_MAYBE_UNUSED                                                          \
    static inline size_t size_of(T)(const T *self) {                          \
        (void)self;                                                           \
        return sizeof(T);                                                     \
    }                                                                         \
    DYN_MAYBE_UNUSED                                                          \
    static inline size_t align_of(T)(const T *self) {                         \
        (void)self;                                                           \
        return _Alignof(T);                                                   \
    }                                                                         \
    IMPL(IOwned, T);                                                          \
    DYN_MAYBE_UNUSED                                                          \
    static inline IOwned as_owned(T)(T *self) {                               \
        return DYN(IOwned, T, self);                                          \
    }                                                                         \
    typedef struct IOwned IOwned

#define OWNED_PLAIN(T)                                                        \
    DYN_MAYBE_UNUSED                                                          \
    static inline void drop(T)(T *self) {                                     \
        (void)self;                                                           \
    }                                                                         \
    OWNED(T)

#endif /* OWNED_H */
