# Libcanard contribution guide

The library shall be implemented in ISO C99/C11 following MISRA C:2012.
Deviations are documented directly in the source code as follows:

```c
// Intentional violation of MISRA: <some valid reason>
<... deviant construct ...>
```

[Zubax C++ Coding Conventions](https://kb.zubax.com/x/84Ah) shall be followed.

You may want to use the [toolshed](https://github.com/OpenCyphal/docker_toolchains/pkgs/container/toolshed)
container for development.

Refer to the CI pipelines to see how to run tests locally.

To release a new version, simply create a new release on GitHub: <https://github.com/OpenCyphal/libcanard/releases/new>
