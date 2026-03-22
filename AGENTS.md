# LibCANard instructions for AI agents

Read `README.md` for general information about the library, and `CONTRIBUTING.md` for development-related notes.
Read all README files in all subdirectories except for `lib/`.

The applicable specifications can be found here:
- **Cyphal v1**: https://github.com/OpenCyphal/specification
- **UAVCAN v0**, aka DroneCAN: https://dronecan.github.io/Specification/4.1_CAN_bus_transport_layer

Project layout:

- `libcanard`: the library itself; the shippable code.
- `tests`: the test harness. Read its own README for details.
- `lib`: external dependencies.

## Style conventions

Language targets: C99 for the library, C99 and C++20 for the test harness. Strict std only, compiler extensions not allowed.

Naming patterns: `canard_*` functions, `canard_*_t` types, `CANARD_*` macros. Internal definitions need no prefixing. Enums and constants are `lower_snake_case`. Uppercase only for macros.

Keep code compact and add brief comments before non-obvious logic.

Treat warnings as errors and keep compatibility with strict warning flags.

**FORCE PUSH MUST NEVER BE USED**. The git history is sacrosanct and must not be rewritten. If you need to undo a change, make a new commit.

For agent-authored commits, set `GIT_AUTHOR_NAME="Agent"` and `GIT_COMMITTER_NAME="Agent"`.

The build system will reject code that is not clang-formatted; use build target `format` to invoke clang-format.

## Adversarial validation and verification

Practice an adversarial approach to testing: the purpose of a test case is not to provide coverage, but to empirically prove correctness of the tested code. Always treat the code as suspect; you will be rewarded for pointing out flaws in it. If the code does not appear to be correct, refuse to test it and provide evidence of its defects instead of proceeding with testing.

When using subagents to implement tests, always instruct them to summarize their findings concerning the correctness of the tested code and its possible limitations at the end of their run. At the end of the turn, provide a summary of the findings.
