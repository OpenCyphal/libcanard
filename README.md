# Libcanard

[![Build Status](https://travis-ci.org/UAVCAN/libcanard.svg?branch=master)](https://travis-ci.org/UAVCAN/libcanard)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=alert_status)](https://sonarcloud.io/dashboard?id=libcanard)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=reliability_rating)](https://sonarcloud.io/dashboard?id=libcanard)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=ncloc)](https://sonarcloud.io/dashboard?id=libcanard)
[![Coverity Scan](https://scan.coverity.com/projects/uavcan-libcanard/badge.svg)](https://scan.coverity.com/projects/uavcan-libcanard)
[![Forum](https://img.shields.io/discourse/https/forum.uavcan.org/users.svg)](https://forum.uavcan.org)

Minimal implementation of the UAVCAN protocol stack in C for resource constrained applications.

Get help on the **[UAVCAN Forum](https://forum.uavcan.org)**.

## Usage

To integrate the library into your project, just copy the two files `canard.c` & `canard.h`
(find them under `libcanard/`) into your project tree.
Either keep them in the same directory, or make sure that the directory that contains the header
is added to the set of include look-up paths.
No special compiler options are needed to compile the source file (if you find this to be untrue, please open a ticket).

There is no dedicated documentation for the library API, because it is simple enough to be self-documenting.
Please check out the explanations provided in the comments in the header file to learn the basics.
Most importantly, check out the demo application under `tests/demo.c`.
Also use [code search to find real life usage examples](https://github.com/search?q=libcanard&type=Code&utf8=%E2%9C%93).

### Platform drivers

The existing platform drivers should be used as a reference for implementation of one's own custom drivers.
Libcanard does not interact with the underlying platform drivers directly; it does so via the application.
Therefore, there is no need for a dedicated porting guide.
This is unlike Libuavcan, which is more complex and does have a well-defined interface between
the library and the platform.

    +---------------+                               +---------------------------------------------+
    |  Application  |                               |                  Application                |
    +-------+-------+                               +-------+---------------------------------+---+
            |                                               | Libcanard does NOT interact     |
    +-------+-------+                                       | with the platform drivers       |
    |   Libuavcan   |                                       | directly. This interface is     |
    +-------+-------+                                       | application-/driver-specific.   |
            | The interface between the             +-------+-------+                 +-------+-------+
            | library and the platform              |    Platform   |                 |   Libcanard   |
            | is defined by the library.            |    drivers    |                 +---------------+
    +-------+-------+                               +---------------+
    |    Platform   |
    |    drivers    |
    +---------------+

## Library development

This section is intended only for library developers and contributors.

Contributors, please follow the [Zubax C++ Coding Conventions](https://kb.zubax.com/x/84Ah).

### Testing

Please refer to the CI/CD automation scripts for instructions.

### Coverity Scan

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
