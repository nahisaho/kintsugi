// Placeholder for musubix3's static graph analysis only.
//
// doctest.h (single-header distribution) contains:
//   #ifndef DOCTEST_SINGLE_HEADER
//   #include "doctest_fwd.h"
//   #endif
// DOCTEST_SINGLE_HEADER is unconditionally defined immediately above that
// block, so this #include is inside dead code and is never actually
// compiled/used in this project's single-header setup. This empty file
// exists solely so that musubix3's compiler-based include-graph checker
// (which does not evaluate preprocessor conditionals) can resolve the
// reference; it has no effect on any real build.
