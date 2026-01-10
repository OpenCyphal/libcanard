# LibCANard instructions for AI agents

Please read `README.md` for general information about the library, and `CONTRIBUTING.md` for development-related notes.

Keep the code and comments very brief. Be sure every significant code block is preceded with a brief comment.

If you need a build directory, create one in the project root named with a `build` prefix;
you can also use existing build directories if you prefer so,
but avoid using `cmake-build-*` because these are used by CLion.
When building the code, don't hesitate to use multiple jobs to use all CPU cores.

Run all tests in debug build to ensure that all assertion checks are enabled.

It is best to use Clang-Format to format the code when done editing.
