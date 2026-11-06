# Extracted and inferred call targets

The local `graph-export` pipeline runs a separate corpus-wide pass after syntax
extraction. It writes graph schema v3. The Python reader accepts v1, v2 and v3;
older readers must be upgraded before opening v3. Re-export existing code schema
v2 models to run both tiers. No re-ingestion, model API or external service is required.

Structural relationships carry `evidence_kind: EXTRACTED`. A call's source spelling,
span and original `resolution_status: unresolved` remain intact. The new
`target_resolution` property records `extracted`, `inferred`, `ambiguous`, or `unresolved`, candidate
IDs, a reason, and the resolver version. `INFERRED_TARGET` connects a call site to
each candidate definition, with `evidence_kind: INFERRED`, status, method, matched
qualified name, evidence node IDs and limitations. A single candidate is still an
inference. Multiple overloads remain separate ambiguous candidates.

`EXTRACTED_TARGET` connects explicit source syntax to a unique visible exported
definition. It carries `evidence_kind: EXTRACTED`, `resolution_status: extracted`,
and one of two methods:

- `explicit_qualified_name`: a qualified spelling such as `Foo::bar()`,
  `ns::helper()` or `::helper()` matches exactly one definition in the searched scope.
- `explicit_this_receiver`: `this->bar()` matches exactly one definition on the
  enclosing class. Out-of-line methods use the caller's qualified name plus a
  unique visible class definition. The class node is included in edge evidence.

These tiers follow Graphify's distinction between explicit receiver/qualifier
evidence and inferred matches. EXTRACTED is an evidence category, not compiler
verification or a calibrated probability. Definition-only indexing cannot rule
out declaration-only overloads, aliases, preprocessing differences or virtual
dispatch. Multiple exported matching definitions stay ambiguous and produce only
`INFERRED_TARGET` edges. Lambda `this` captures, unknown classes and ambiguous
class identities are not guessed. Ordinary `obj.method()` calls still need a
future receiver-type pass.

The initial pass matches simple direct or qualified names, searching enclosing
namespace/class scopes before global scope. Absolute `::` spellings start globally.
Candidates come from the same file or transitively reachable relative include
paths within the same repository, revision, grammar and extractor version. Include
path matching is a heuristic, not preprocessor resolution: the current model does
not retain quote-versus-angle delimiters or build search paths. Include cycles are
bounded. Partial caller/target files are excluded. Expressions requiring receiver
typing, templates, operators or indirect-call analysis remain unresolved.

This pass does not perform local-binding/shadowing analysis, overload selection,
argument-dependent lookup, alias resolution, declaration/definition unification,
preprocessing, linkage checking or type inference. Name matches can therefore be
false positives, even with one candidate. It emits neither `CALLS` nor a claim of
semantic resolution or complete impact analysis. A missing candidate does not
prove a function is never called.

Retrieval must explicitly request `EXTRACTED_TARGET` or `INFERRED_TARGET`; default traversal remains
structural. Returned edges retain evidence and inference warnings. For example,
follow `HAS_CALL_SITE` then `INFERRED_TARGET` to inspect possible callees. The reader
rejects target edges with missing evidence or evidence crossing revision boundaries,
and rejects extracted targets with ambiguous alternatives or invalid method/status labels.

This pass currently enriches local JSON exports, not optional Neo4j projections.
Future passes can add binding/type evidence or compiler-backed targets without
discarding the syntax facts. Keep proven targets distinct from these candidates.
