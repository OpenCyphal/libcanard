# Libcanard
[![Build Status](https://travis-ci.org/UAVCAN/libcanard.svg?branch=master)](https://travis-ci.org/UAVCAN/libcanard)
[![Coverity Scan](https://scan.coverity.com/projects/uavcan-libcanard/badge.svg)](https://scan.coverity.com/projects/uavcan-libcanard)
[![Gitter](https://img.shields.io/badge/gitter-join%20chat-green.svg)](https://gitter.im/UAVCAN/general)

Minimal implementation of the UAVCAN protocol stack in C for resource constrained applications.

Links:

* **[DOCUMENTATION](http://uavcan.org/Implementations/Libcanard)**
* **[DISCUSSION GROUP](https://groups.google.com/forum/#!forum/uavcan)**

## Usage

If you're not using Git, you can just copy the entire library into your project tree.
If you're using Git, it is recommended to add Libcanard to your project as a Git submodule,
like this:

```bash
git submodule add https://github.com/UAVCAN/libcanard
```

The entire library is contained in two files: `canard.c` and `canard.h`.
Add `canard.c` to your application build, add `libcanard` directory to the include paths,
and you're ready to roll.

Also you may want to use one of the available drivers for various CAN backends
that are distributed with Libcanard - check out the `drivers/` directory to find out more.

Example for Make:

```make
# Adding the library.
INCLUDE += libcanard
CSRC += libcanard/canard.c

# Adding drivers, unless you want to use your own.
# In this example we're using Linux SocketCAN drivers.
INCLUDE += libcanard/drivers/socketcan
CSRC += libcanard/drivers/socketcan/socketcan.c
```

## Library Development

This section is intended only for library developers and contributors.

The library design document can be found in [DESIGN.md](DESIGN.md)

Contributors, please follow the [Zubax Style Guide](https://github.com/Zubax/zubax_style_guide).

### Building and Running Tests

```bash
mkdir build && cd build
cmake ../libcanard/tests    # Adjust path if necessary
make
./run_tests
```

### Submitting a Coverity Scan Build

First, [get the Coverity build tool](https://scan.coverity.com/download?tab=cxx).
Then build the tests with it:

```bash
export PATH=$PATH:<coverity-build-tool-directory>/bin/
mkdir build && cd build
cmake ../libcanard/tests -DCMAKE_BUILD_TYPE=Debug   # Adjust path if necessary
cov-build --dir cov-int make -j8
tar czvf libcanard.tgz cov-int
```

Then upload the resulting archive to Coverity.
