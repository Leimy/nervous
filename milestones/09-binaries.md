# Milestone 09 - Basic Binaries

## Dependency note

Milestones 04 and 08 must complete before binary ownership integration. Review R3 follows this milestone and gates multicore work.

## Goal

Exercise the pattern compiler and allocator with deterministic binary construction and exact left-to-right binary matching.

## Read first

- `../docs/semantics.md`
- `../docs/questions.md`, section "Milestone 09"
- `04-patterns.md`
- `08-memory.md`

## Initial feature set

- Integer segments with explicit widths.
- Unsigned big-endian default.
- Explicit signed/unsigned and big/little modifiers.
- Sized byte-aligned binary segments.
- A final unsized `/binary` remainder.
- Whole values and `/binary` segments byte-aligned.

Arbitrary-width integer segments may be supported if they do not force full unaligned bit-string storage.

## Semantics

- Segments are processed left to right.
- A size may use an enclosing binding or a variable bound by an earlier segment.
- No binary-pattern backtracking occurs.
- A pattern without a final remainder must consume the complete value.
- Tentative segment bindings obey the same transactional rules as all other patterns.

## Required tests

- Exact match and trailing-byte rejection.
- Empty and nonempty remainders.
- Length-prefixed payload.
- Endian and signed decoding.
- Invalid forward size reference rejected by the frontend.
- Late failure rolls back earlier segment bindings.
- Allocation and size-limit failures are controlled.

## Exit criterion

Binary construction and matching are usable for a small length-prefixed protocol and share normal process-heap ownership.

## Not in scope

General unaligned bit strings, shared large-binary storage, compression, text semantics, or external buffer ownership.

## Implementation state

T01-T04 implemented and accepted (user confirmed `rc tests/run.rc` passes); T05 (latency measurement) remains. Deviations from the settled interfaces below, as the plan asks to report:

- The AST kinds are `Ebinagg` (aggregate) and `Ebinseg` (segment), not `Ebin`. No `flags` field was added to `Expr`: a segment packs its shape into `ival` as `(kind<<8) | (width<<2) | (signed<<1) | little`, with kind `Binsegint`/`Binsegsized`/`Binsegrest` (`include/nervous.h`); a sized segment's size is the expression in `right` (an `Eint` or `Evar` in patterns, any primary or unary expression in construction). Note that the opcode `width-flags` operand uses the other bit order (bit 0 signed, bit 1 little); `lib/compile.c` converts.
- Size direction is checked twice: `nvpatternfromexpr` rejects a size bound by the same or a later segment of one binary, and `nvpatterncode` (`lib/patbc.c`) rejects any size variable not yet bound when its segment is lowered, which also catches a size bound later in an enclosing pattern (`${<<p:n/binary>>, n}`).
- Construction evaluates all segment operands left to right, then emits `binalloc` and one append per segment (a chain of copies, D077 as amended by T01), rather than a single pre-sized allocation.

Required tests, and where each is covered:

| Required test | Coverage |
|---|---|
| Exact match and trailing-byte rejection | `tests/pattern/parsetest.c` cases b, c; `tests/frontend/binfault-trailing.nv` |
| Empty and nonempty remainders | parsetest d; `tests/frontend/binary-run.nv`; `examples/protocol.nv` |
| Length-prefixed payload | parsetest e; binary-run (`unframe`); protocol |
| Endian and signed decoding | parsetest f; binary-run; `tests/process/exectest.c` case A |
| Invalid forward size reference rejected by the frontend | parsetest h; `binreject-forward.nv`, `binreject-unbound.nv` |
| Late failure rolls back earlier segment bindings | parsetest i; binary-run (a `match` whose first clause fails on its second segment) |
| Allocation and size-limit failures are controlled | `binfault-limit.nv` (`-H 1000`, `system_limit`), `binfault-toolong`/`negsize`/`notbin` (`bad_binary`), `binfault-overflow`/`signed` (`overflow`), `binfault-badarith`; exectest case E |

Exit criterion: `examples/protocol.nv` sends chunked length-prefixed frames between two processes; each chunk is copied into the consumer's process heap and decoded there. Run as a regression by `tests/frontend/run.rc` through both `-r` and saved bytecode (`-c` + `-X`).

## Settled interfaces (coordinator, pre-implementation)

D076-D079 settle the semantics; these are the concrete shapes M09-T01 (runtime) and M09-T02 (frontend) build to, so the two tasks stay disjoint. Field names are the interface; an implementer may rename but must preserve the meaning and report the rename.

**`include/nvpat.h` (T01 owns; T02 builds to it).** The pattern kind enum gains `Pbin` after `Ptuple`. `NvPattern` gains two fields used only when `kind == Pbin`: `NvBinseg *seg; int nseg;` (a segment list; `elem`/`n` remain tuple-only). The new struct, one entry per source segment in order:

```c
enum { Bsegint, Bsegsized, Bsegrest };
enum { Bsegsigned = 1, Bseglittle = 2, Bsegtest = 4 };
struct NvBinseg {
	int kind;       /* Bsegint, Bsegsized, Bsegrest */
	int width;      /* Bsegint: byte count 1, 2, 4 or 8; 0 otherwise */
	int flags;      /* Bsegint: Bsegsigned, Bseglittle, Bsegtest (see below) */
	char *name;     /* Bsegint: bound variable; Bsegrest: bound variable or nil */
	char *sizename; /* Bsegsized: variable holding the byte count, or nil */
	vlong sizeval;  /* Bsegsized: literal count when sizename is nil */
	vlong ival;     /* Bsegint + Bsegtest: the literal value the bytes must encode */
};
```

A `Bsegint` resolves to exactly one of three meanings, decided by `name` and the `Bsegtest` bit so that a literal test value of `0` is not ambiguous with "no test": `name != nil` binds the decoded value to `name`; `name == nil && (flags & Bsegtest)` tests that the bytes decode to `ival` (a mismatch selects the next clause, D077); `name == nil` and no `Bsegtest` discards the value (equivalent to `_ : W`). A `Bsegrest` binds all remaining bytes to `name`, or discards them when `name == nil`. The `NvBinseg` list is freed by `nvpatternfree` (T01 extends it; the existing `patternclear` walks `elem` only, so the binary cleanup is a sibling case).

**`include/nervous.h` (T02 owns; T03 builds to it).** The expression kind enum gains `Ebin` (a binary aggregate; `list` holds the segments) and `Ebinseg` (one segment). A segment's `Expr` uses: `left` = the value expression (integer expression for `Ebinseg` integer segments, binary expression for binary segments), `ival` = the width in bytes (integer segments) or the literal size (sized binary segments, when the size is a literal), `text` = the size variable name (sized binary segments, when the size is a variable; nil otherwise), and a new `int flags` field on `Expr` (bit 0 signed, bit 1 little-endian; 0 for every existing kind, so no existing code path is affected). A segment whose value is a variable (pattern mode) or literal (integer test) is the same `Ebinseg` shape with `left` an `Evar`/`Ewild`/`Eint`. The parser does not validate pattern-mode legality or forward size references; `patternok` (parse.c, T02) and `nvpatternfromexpr` (patcompile.c, T02) enforce the D076/D077 pattern restrictions, and the size-reference direction (a size may name only an enclosing binding or a variable bound by an earlier segment) is a frontend error at `nvpatternfromexpr`, per the milestone's "Invalid forward size reference rejected by the frontend" required test.

**Bytecode (T01 owns; T02's lowering emits, T03's construction emits).** The three allocation opcodes carry their operands in the existing four-int `NvInsn` shape; the exact operand roles are T01's to finalize against D076-D079 and to record in `docs/bytecode.md` (T01's write set). T02's pattern lowering and T03's construction lowering must agree with that record, which is why T03 depends on T01's settled opcode contract rather than on T01's code.

The runtime matcher (`lib/pattern.c`, T01) matches `Pbin` against a `Bbin` term left to right with no backtracking (D077), using `nvbinlen`/`nvbinbytes` (T01) and binding a segment's bytes via a self-contained copy (T01). The pattern harness (`tests/pattern/ptest`) and the source path share this matcher, so a binary pattern's transactional rollback comes from the existing `nvpatternmatch` temporary-bindings mechanism (D003) with no new rollback code.
