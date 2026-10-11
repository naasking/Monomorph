# Monomorph as a C compilation target: design notes

Recorded 2026-10-09. The goal is a "safe" foundation for using C as the compilation target of higher-level languages, for instance through tagless-final interpreters. High-level features compile down to name-mangled C, so that C serves as a portable backend. The library stays C11, with nothing newer required.

**Since 2026-10-11, interfaces are types.** `INTERFACE(IShape)` defines `IShape` itself, named with C#'s `I` prefix, and `Dyn(I)` is gone. Interfaces can also be generic, with `INTERFACE_OF(IComparer, T)` and `IMPL_OF`. Entries below written before then say `Dyn(I)` for what is now the interface type `I`.

## Framing

Once C is a target, soundness comes from the front end's type checker. The macros' job is to:

- fix the runtime conventions: layouts, names and calling protocol;
- give the generated C a second, independent type check, which catches compiler bugs.

So the gaps that matter most are the ones a careful front end can't work around on its own.

## Open items, in priority order

### 1. Ownership and memory

- `Dyn` and `Fn` only borrow, and there is no destroy protocol. [`samples/tagless.c`](samples/tagless.c) leaks memory to get closures that outlive the call that made them.
- Owned forms, like Rust's `Box<dyn>` and `Rc<dyn>`, need `drop`, `size` and `align`. With these, code that holds a `Dyn` can move the object into storage it controls and clean it up, without knowing its type. `align` matters only for storage the caller manages, such as arenas and pools, since `malloc` already aligns for any type.
- **They need no change to `dyn.h`** (tried 2026-10-10, clang and MSVC). They can be their own interfaces: `Layout` (`size_of`, `align_of`) and `Drop` (`drop`), combined by method list into `Owned`. A domain interface reaches them through one method, `METHOD(owned, Dyn(Owned) FARGS())`, which returns the same object's `Dyn(Owned)`. That costs one pointer per object, the two views agree by construction, and ownership code is written once against `Dyn(Owned)`. Rust reaches a supertrait's table the same way. The alternatives are holding a `Dyn(Shape)` and a `Dyn(Owned)` side by side (an extra pointer, and nothing makes them agree), or including the methods in each interface (one table, but ownership code must be generic over the interface).
- **Now an example, not core** (2026-10-10). [`samples/owned.h`](samples/owned.h) has the interfaces, `AsOwned_METHODS` and the `OWNED`/`OWNED_PLAIN` helpers. [`samples/owned.c`](samples/owned.c) has a region written once against `Dyn(Owned)` that owns objects from two interfaces. Open: whether to promote it beside `dyn.h`. `Fn` has no table to reach it from, so owned closures need another route.
- **Adding them by default to every `Dyn` requires deriving them automatically** (the user's condition, 2026-10-10). Findings, checked with clang and MSVC:
  - `size_of` and `align_of` can be derived from `sizeof(T)` and `_Alignof(T)`, except for opaque types. `IMPL` works with incomplete types today, and automatic layout would break that. Flexible array members and types that can't be moved (intrusive list nodes, pthread mutexes) have correct numbers, but moving them by `memcpy` is wrong.
  - `drop` can't be derived: a C struct doesn't say what it owns, and C can't check whether `drop(T)` exists. The options are:
    - a no-op default plus a marker per owning type, `#define DROPS_T OWNS`, which works but leaks silently if the marker is forgotten;
    - requiring `drop(T)` for every type, so forgetting it is a compile error, which is free for a compiler front end;
    - deriving `drop` from a record macro's field list, with owning pointer types.
  - Assessment: `Owned` doesn't meet the bar for a default on hand-written C, so keep it opt-in.
- Then pick a policy: reference counting (which leaks cycles), regions (one per evaluation suits tagless final), or a tracing GC with a root protocol. The headers can stay neutral if tables carry `drop` and `trace` slots.

### 2. Undefined behaviour in generated code

- **Integer arithmetic.** Signed overflow, `INT_MIN / -1` and oversized shifts are UB, and `Eval`'s `add` in `tagless.c` is UB on overflow. C11 has no `ckd_add` (that's C23), so the library needs checked and wrapping primitives: `__builtin_add_overflow` where the compiler has it, with a portable fallback.
- **Evaluation order.** C leaves the order of argument evaluation unspecified. For left-to-right semantics, the compiler should emit ANF, with every intermediate value in a named temporary.
- **Macro hygiene.** ANF fixes this too. `CALL` evaluates `f` twice, and `APP` in `tagless.c` writes `f` out twice, so nested applications in the function position double in size at each level when preprocessed.
- **Bounds.** Arrays need a `Slice(T)` with checked indexing.
- **Erasure.** Casts through `void *` are only as safe as the discipline around them (see 4).

### 3. Control flow

- **Tail calls.** C11 doesn't guarantee them, so functional languages and CPS-style output overflow the stack. `tagless.c`'s `Eval` also recurses on the C stack. Options: trampolines, Cheney on the MTA (as Chicken Scheme does), or `musttail` where the compiler has it (Clang, GCC 15) with a fallback.
- **Exceptions.** `setjmp`/`longjmp` skips cleanup. Compile exceptions to `Result`-style returns, or pair unwinding with regions.
- **Generators, async and effects** need a state-machine or CPS transform. A closure over a state struct is already a coroutine frame; `Counter` in [`samples/closures.c`](samples/closures.c) is a minimal one.

### 4. Erasure

Every erased value needs two things at run time:

- **A uniform representation.** A pointer, or a fixed-size inline buffer as Swift uses.
- **A dictionary of operations.** These are the methods its constraints promise. Once a value is held by something that owns it, this also includes size, alignment, copy and drop.

`Dyn(I)` is `{ void *self; const Vtable(I) *vt; }`, which is already an existential package, ∃T. I(T). So `Dyn` covers ∃. It doesn't fit ∀, which wants the dictionary kept apart from the values:

```c
/* ∃: one value and its dictionary, travelling together */
Dyn(Show) xs[n];                       /* each element may be a different T */

/* ∀: one dictionary per type variable, shared by every value of that type */
void sort(const Witness *T, const Vtable(Ord) *ord, void *xs, size_t n);
/* Witness = { size, align, copy, drop }, i.e. Swift's value witness table */
```

Packing each element of a `∀T. List T` into a `Dyn` has three problems:

- It costs a word per element.
- It loses the guarantee that all elements are the same T. This is why binary methods like `compare(Self, Self)` aren't object-safe: two `Dyn`s may hold different types.
- It can't return `Self` by value without a witness table, because nothing says how large the value is.

The cases:

1. **Existentials.** "Some type implementing I." `Dyn` handles these today.
2. **Rank-N values.** `Program` in `tagless.c` is `∀r. Lang r ⇒ r Int`, encoded as a C function that takes a `Dyn(Lang)`. But `r` escapes into the result, so the result has to be erased as well. That's why `Repr` holds a `void *`.
3. **Polymorphic recursion, and generic code compiled once,** for example a generic function exported from a separately compiled library. Both need ∀ dictionary passing with erased values.
4. **Generic methods in interfaces,** like `lam<A,B>`. Either one slot per instance, which needs a finite, known set of instances (as `tagless.c` does), or one erased slot that takes witness tables at run time (as Swift does). For `lam`, the per-instance slots exist only so C can type-check them. A single erased slot would work, but it needs an erased closure type, and calling through a mismatched function-pointer type is UB. So it needs generated adapter functions, not casts.
5. **Associated types behind `dyn`,** like `repr`. Erased to opaque handles, which are sound only if a handle never reaches a different implementation than the one that made it. An optional safeguard: each handle records its creator and checks it on use. That's a tag for the interpreter, not for the object-language type.

**Status (2026-10-10): not in the core headers; possibly an opt-in extension, undecided.** Dictionary passing with tables built at run time is in the first place a strategy for a runtime built on Monomorph. If the library offers it as an opt-in extension, the people who opt in need control over storage, because conformances of generic types require tables built at run time. Exploring that is open.

**Superseded 2026-10-11 by generic interfaces.** The user observed that a witness is C#'s `IEqualityComparer<T>` pattern: an ordinary interface whose receiver is a comparer object rather than a `T`. `dyn.h` now has generic interfaces (`INTERFACE_OF`, `IMPL_OF`), and [`samples/comparers.c`](samples/comparers.c) replaces `module.h`/`module.c`, which were removed.

- **When `T` is known at compile time,** operands are typed and checked by C.
- **Comparers are values with a real receiver,** so they can carry state, a type can have several, and static methods are ordinary methods.
- **Erased code** instantiates the same interfaces at `Any` (`void`).
- **Bundling several of a type's interfaces,** as `Module` did, is now a plain struct of interface values, as `Erased(T)` and `Column` do in that sample.

Findings on the generic-interface form (2026-10-11, clang and MSVC; scratch `polyrec.c` and `convention.c`, not in the repo):

- **Polymorphic recursion is cheaper than before.** A strategy has a receiver, so `Vec T`'s strategy is one static table plus a small object holding `T`'s strategy, and that object can live on the recursion's own stack frame. No tables are built on the heap, as `witness.c` needed, and values carry no table pointer, as `module.c` needed. The catch is that such a strategy is borrowed from its frame.
- **Phantom types are checked more thoroughly.** Typed instances check every operand as a C type: a mismatched pointer is a warning in clang, a mismatched value an error. Erased instances erase phantoms as before.
- **Calling conventions.** Interfaces over values keep monomorph's convention: `area(IShape)` matches `area(Circle)`. Strategy interfaces use the same convention with the strategy as receiver. Their static counterpart is a concrete strategy type, so generic code parameterised by the comparer type (`DEFINE_SORT(T, C)`) works for both. A generic adapter like `Natural(T)`, C#'s `Comparer<T>.Default`, turns a method on the value into a strategy at no run-time cost.
- **Coherence is the user's choice.** Nothing ties a collection to the comparer it was built with. Either the comparer type goes in the container's type (`SortedSet(int, Ascending)`, as C++'s `std::set` does), or the comparers are compared at run time. Monomorph supports both, and the library shouldn't pick one.

These and the other pitfalls (borrowed strategies, binary methods across separate values, `-Werror=incompatible-pointer-types`, identity under identical-code folding, null receivers) are in the README under `dyn.h`, "Pitfalls".

The record of the intermediate design follows.

**An opt-in form needing no changes and no tables built at run time** (2026-10-10, on unchanged `dyn.h` plus `owned.h`, clang and MSVC). It was `samples/module.h` with `samples/module.c`. `Module(I0, I1, ...)` was a static set of one type's tables, its `Value` table first, then one per interface. Generic values carried a pointer to one, which lifted the "carries only one table" limit below. There was a declare/define split for one module per program.

- **Value witnesses are an interface:** `Value_METHODS` = `Layout_METHODS`, `Drop_METHODS` and `clone`. Conformances used by erased code include it by method list.
- **`Self` parameters beyond the receiver** are written `const void *`, with helper macros generating the erased methods from typed ones.
- **Methods that ignore the receiver** are called with `NULL`. `Self` results go through a `void *ret` parameter.
- **Generic code calls slots directly,** parenthesised.
- **Generic types' values carry their parameter's table,** so `Vec`'s tables are static and the same for every `T`. Polymorphic recursion runs through one static table, with no allocation.
- **Trade-off:** one pointer per generic value, and a value carries only the tables in its module type. A `Vec` that carries `Module(Print)` can't use `Ord`. With `Module(Print, Ord)` it can, so a compiler picks each container's module type from the interfaces its code needs.
- **Generic types' tables are static only if their layout doesn't depend on `T`.** `Vec` qualifies because its elements are out of line. A `Pair T U` with inline fields needs those fields out of line, tables built at run time, or monomorphisation.
- **The only addition to `dyn.h` worth considering** was a public `VTABLE(I, T)` accessor, added 2026-10-11. Binary methods and multimethods are likewise the runtime author's concern. Statically, `OVERLOAD(name, A, B)` already names one instance per pair of types, and `Dyn` can do double dispatch in the visitor style.

[`samples/witness.c`](samples/witness.c) is the reference for a runtime that wants dictionary passing. It is essentially Haskell's scheme: conformance tables are dictionaries, and instance contexts like `Print T ⇒ Print (Vec T)` are dictionary functions. It adds value witnesses, which Haskell doesn't need because its values are boxed. It builds everything from `monomorph.h`'s naming alone, without `dyn.h`, which shows the naming scheme is enough of a foundation. Things a runtime doing this has to decide:

- **`Self` parameters** are passed by pointer, and a `Self` result is written to caller-provided storage. Concrete methods stay ordinary, with adapters translating.
- **Every slot takes its own table first,** so tables built at run time can reach the tables they captured.
- **Slot calls must be written `(w->m)(...)`** (`VCALL` in the prototype), or method-name macros like `print(T)` capture them.
- **Only conformances of generic types are built at run time.** Concrete conformances are static tables. Caching the built ones is optional: GHC usually rebuilds dictionaries rather than caching them.
- **Type identity across translation units.** With `IMPL`, each file has its own static tables, so identity must go by name (`MONOMORPH_TYPE_NAME`). With `IMPL_DECLARE`/`IMPL_DEFINE`, or witnesses defined once per program, table addresses work. Identical-data folding (MSVC `/OPT:ICF`) could in principle merge two types' tables.
- **Inside erased code, C's type checking is gone,** so only the front end guarantees that values and dictionaries match.

### 5. Algebraic data types: settled, use final encoding

This is not a gap. In final (Böhm–Berarducci) form, a match is an interpreter, and a missing case is a missing method, so `IMPL` rejects it. Even before GCC 14, `&OVERLOAD(name, T)` on an undeclared name is a hard error, not an implicit declaration.

Trade-offs:

- **Folds versus case analysis.** Böhm–Berarducci gives folds, so a shallow match like `tail` or "is this empty?" costs O(n). Scott encoding gives one-step case analysis. It needs recursive types, which C allows.
- **Data built at run time is a rank-2 value,** `∀r. ListAlg r ⇒ r`, so final-form data depends on erasure (item 4, case 2).
- **Two scrutinees at once,** as in `zip`, `merge` or `eq`, means folding one value into a function that consumes the other. Pattern-match compilation still happens, targeting nested interpreters instead of a switch.
- **Cost.** Every constructor application is a heap-allocated closure, and a match is two indirect calls. There are no unboxed sums, and no null pointer standing for "none".

Lowering: defunctionalise (Reynolds). A Scott-encoded value's code pointer can only be one of its constructors. Defunctionalising those closures gives a tagged union, plus a dispatcher that switches on the tag and calls the visitor's method. The compiler generates both sides, so the switch is exhaustive by construction. Tagged unions become a compiler optimisation, not a library feature.

### 6. Interfaces: object safety and composition

- `dyn.h` doesn't enforce object safety (no generic methods, no `Self` by value, no binary methods), so the front end must.
- Missing:
  - interface inheritance and upcasting, so a `Dyn(AB)` can become a `Dyn(A)` (Rust embeds the parent's vtable pointer);
  - combined `dyn A + B`;
  - downcasting or `Any`.
- **Type identity.** `IMPL` emits a `static` vtable in each translation unit, so its table addresses aren't type identities. `IMPL_DECLARE`/`IMPL_DEFINE` (added 2026-10-10) give one table per program, whose address is. Other options, from fastest to most open:
  - dense integer IDs from one `X(T)` list, which make a multimethod a 2-D table lookup (checked in a scratch `multi2.c`, not in the repo);
  - the address of one `extern` object per type;
  - names hashed once at run time.

  `__LINE__` and `__COUNTER__` aren't unique across files.

### 7. Instantiation across translation units

- `ARROWS` in `tagless.c` is a hand-written list of instances. A compiler would compute it.
- Structural types (tuples, `Fn` signatures) must be defined once per translation unit and identically everywhere, as `FN_DEFINE` already arranges.
- Non-inline instances need a single home, or you accept a copy in every translation unit. Portable C has no weak or "link once" linkage. The usual answer is a generated, whole-program `instances.h`.

### 8. Closures

Missing:

- owned closures that can escape (item 1);
- recursive closures;
- mutable captured variables shared between closures, which need boxed cells (standard closure conversion);
- memoised thunks, for lazy languages.

### 9. Portability of the names

- Some embedded and older compilers reject non-ASCII identifiers. C11 also guarantees only 31 significant characters in external identifiers, and each non-ASCII character counts as 6 or 10 of them.
- `MONOMORPH_OPEN`, `SEP` and `CLOSE` are configurable, so an ASCII mode is possible. The compiler would have to escape source identifiers so the delimiter can never appear in them, keeping names unambiguous.

### Smaller items

- `#line` directives so errors and debuggers point back at the source language.
- `Send`/`Sync`-style markers if closures or `Dyn` values cross threads. C11's threads are optional.
- Make `-Werror=implicit-function-declaration` mandatory, since a missing `OVERLOAD` instance is otherwise only a warning before GCC 14.

## Next steps

1. **Ownership and checked arithmetic, together.** Decide whether the ownership interfaces (`Layout`, `Drop`, `Owned`) ship as an opt-in header, and add checked-arithmetic primitives. Test: port `tagless.c` so it frees its memory and traps on overflow.