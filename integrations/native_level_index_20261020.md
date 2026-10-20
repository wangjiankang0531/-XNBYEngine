# Exact LEVEL bounds and complete descendant frontiers

The Zhumadian near view showed coarse upper corners after 20 seconds. Added a header-only, conservative frustum BVH over exact source subtree envelopes. A child can extend outside its parent geometry, so callers must provide subtree unions, not infer envelopes from a coarse mesh. Invalid index construction preserves the previous index.

NativeSourceFrontier::prepareDescendants accepts an audited strict descendant cut, checks whole-branch coverage and ancestor overlap against the original graph, and commits only after every upload part is ready. Existing immediate-child prepare semantics remain unchanged. Candidate and old resident bytes must fit concurrently; this API does not increase budgets.

Independent MSVC C++20 Release fixture tests/landscape_index runs 12 tests, including the original frontier tests. Randomized 100000-entry/20-camera BVH results match an independent eight-corner oracle; boundary/near-plane/vertical-camera cases and incomplete, overlapping, over-budget and cancelled transactions are covered. This does not establish whole-scene visual quality or runtime performance.

Application integration is an isolated source overlay and candidate package. Whole-viewport scheduling, exact original-source CPU/GPU checks and moving-camera acceptance are tracked separately; no production engine libraries are replaced by this fixture. The standalone fixture avoids the pre-existing held upstream test executables and unrelated uncommitted asset/analysis changes.

The team upstream master was checked again on 2026-10-20 and remains da9898f3054f898924dc1637771eb2947e2d5131. The previous motion-module import is unchanged; this is not a claim that all 59 upstream paths have been merged.
