# Contributing to XS

Thanks for wanting to help out.

## Building

You just need gcc and make. Then:

```
make
```

The compiler builds itself. No extra steps needed.

## Running tests

```
make test
```

This runs everything in the `tests/` directory.

## Adding a test

Drop a new file in `tests/` following the `test_*.xs` naming convention.
The test runner picks them up automatically. Look at existing tests
for the general pattern -- keep them focused on one thing.

## Code style

- C11 standard
- 4-space indentation, no tabs
- Keep functions short and readable
- Don't over-engineer things -- simple, short, and sweet.

## Submitting changes

1. Fork the repo
2. Make your changes on a branch
3. Make sure `make test` passes
4. Open a PR against `main`

If you're tackling something big, it's worth opening an issue first so
we can talk about the approach before you write a bunch of code.

## That's it

No CLA, no lengthy process. Just write good code and don't break stuff.
