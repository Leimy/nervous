# Contributing

Follow ordinary Plan 9 C conventions:

- include `<u.h>` before `<libc.h>`;
- use tabs for indentation and brace style consistent with the existing source;
- prefer small modules with explicit ownership over premature framework layers;
- report errors with useful context and return controlled failure rather than aborting;
- keep public interfaces narrow and place shared declarations in a header only when a second module needs them;
- do not add portability wrappers for systems Nervous does not yet target.

Run `mk` from the repository root after changing C source. The build only verifies compilation; run produced programs and behavioral tests manually.

Nervous source has one canonical layout. Use `nervous -f` for current syntax or `nervous -F` to rewrite syntax v1 to current syntax; do not introduce formatter style options. Formatting is stdout-only. It preserves comment text and order, though trailing comments may move to standalone lines; redirect to a new file and inspect it.

Follow `COORDINATION.md` when more than one agent or developer is active. In particular, never edit outside an assigned exclusive write set.
