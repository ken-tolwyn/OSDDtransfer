# Contributor guidance

## Scope

These instructions apply to the whole repository.

## Engineering rules

- New control-plane and transfer components are C++23.
- Keep the content-addressed store independent from ecosystem repository layouts.
- Treat generation manifests as authoritative and SQLite as disposable derived state.
- Keep tests deterministic and independent of Kubernetes, a network, and external registries.
- Build out of tree with CMake and add focused tests for changes to repository semantics.
- Do not add environment-specific hostnames to authoritative manifests.

## Validation

Run `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure`.
