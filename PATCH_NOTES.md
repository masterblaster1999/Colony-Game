# Colony-Game patch notes

This zip contains **only the changed/new files** from the suggested improvements.

## One manual delete is required (Windows-safe repo fix)

Please delete this file from your repo (it conflicts with Windows case-insensitive paths):

- `src/ai/pathfinding.hpp`

It has been replaced by:

- `src/ai/TimeSlicedPathfinder.hpp`

If you do not delete the old file on GitHub, Windows checkouts can break and CI will fail the new check.
