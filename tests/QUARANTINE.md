# Quarantined Tests

Tests in this file are skipped by the default test runner. They are skipped
to keep the merge queue green while a flake or known regression is being
investigated. Quarantine is an incident-response tool, not a workaround:

- Every entry **must** link to a tracking issue.
- The owner is responsible for keeping the issue current.
- Entries older than 30 days fail the per-PR build until they are either
  fixed or explicitly re-upped with a comment.
- The nightly CI run uses `--no-quarantine` and fails loudly if a
  quarantined test starts passing again. That is the signal to delete
  the entry, not to ignore it.

## Format

Each entry is a top-level `- test_name` line. Everything else on the
entry is metadata for humans. The runner only parses the test name
(matching `[A-Za-z0-9_]+`). Lines outside fenced code blocks that do
not begin with `-` are ignored.

```
- example_test_name
  issue:  https://github.com/kokkoniemi/rotide/issues/0000
  since:  2026-05-16
  owner:  @kokkoniemi
  notes:  why this is quarantined; what investigation has shown
```

## Active entries

<!-- Add new entries below this line. Keep them sorted by quarantine date,
     newest first. Each entry follows the format shown above. -->
