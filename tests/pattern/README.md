# Transactional pattern matcher tests

Build and run the standalone matcher harness:

```text
mk patterntest parsepatterntest patternbctest patterncompiletest
rc tests/pattern/run.rc
```

The harnesses cover repeated-variable structural equality, preservation of existing bindings after failure, exact and nested tuples, rollback after late failure, atom literals, source-order clause selection, independent arities, parsed AST pattern conversion, and parsed fallback clauses. Primitive pattern-to-bytecode lowering is checked for stable instruction shape and verifier compatibility. The compiler harness exercises parsed asserted bindings, ordered function clauses, exact arity, `match` rollback/fallback, and the `match_fail` / `function_clause` reasons.
