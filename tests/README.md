# Test suite

The test suite is composed of two parts: intrusive tests in C that directly `#include <canard.c>` to reach and test the internals, and API-level tests in modern C++ that use the public API only and test the library as a black box. It is preferable to test all behaviors through the API only, and resort to intrusive tests *only when necessary* to reach internal logic that cannot be tested through the API.

All tests are mandatory. The default build and `ctest` execution must run the complete suite; there are no optional/extended tiers that can be skipped.

If you need a build directory, create one in the project root named with a `build` prefix; you can also use existing build directories if you prefer so. Do not create build directories anywhere else.

Static analysis (Clang-Tidy, Cppcheck, etc) must be enabled during build on all targets except external dependencies (e.g., the test framework). In particular, Clang-Tidy and Cppcheck MUST BE ENABLED on the test suite and the Cy library itself at all times.

When compiling, use multiple jobs to use all CPU cores.

Run all tests in debug build to ensure that all assertion checks are enabled. Coverage builds should disable assertion checks to avoid reporting of uncovered fault branches in assert statements.

Refer to the CI workflow files and `CMakeLists.txt` for the recommended practices on how to build and run the test suite.
