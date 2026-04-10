# lia

A post-C/C++ systems programming language.

I am aware `lia` sounds a lot like `lua`. I realized this halfway through
another renaming session. This means that the language may change names again
sooner rather than later.

## How to build

You need

- `cmake` 3.27 or newer. Older versions may work too but your will have to
adjust the minimum required version on the first line of `CMakeLists.txt`.
- `gcc` 14.2 or newer. Theoretically `clang` should mostly work but making the
practice hasn't been done yet. On MacOS `gcc` 15 is available from `brew` at
the time of writing. Fedora 43 comes with `gcc` 15, but unfortunately Ubuntu
24.04, which is the current LTS, has `gcc` 13. 14 is available in the LTS as
`gcc-14`.
- [QBE](https://c9x.me/compile/). QBE is included in Fedora's package manager
and MacOS `brew`. At the time of writing it is in Debian Stable, but
unfortunately not in Ubuntu, so you can download a `.deb` from Debian Stable.
- `python3` for the test runner.

### Building on Linux
```
    $ mkdir build
    $ cd build
    $ cmake .. # I add -GNinja but this is not necessary
    $ cd ..
    $ cmake --build build --target install
    $ export LD_LIBRARY_PATH=`pwd`/build/lib:$LD_LIBRARY_PATH
    $ cd test
    $ ./run_tests.py -a
```

If you need to use a non-standard compiler, specify it using
`CMAKE_CXX_COMPILER` variable in the `cmake` invocation (`-GNinja` optional):
```
    $ cmake -DCMAKE_CXX_COMPILER=gcc-14 -GNinja ..
```

### Building on MacOS
```
    $ mkdir build
    $ cd build
    $ cmake -DCMAKE_CXX_COMPILER=g++-15 ..
    $ cd ..
    $ cmake --build build --target install
    $ export LD_LIBRARY_PATH=`pwd`/build/lib:$LD_LIBRARY_PATH
    $ cd test
    $ ./run_tests.py -a
```

## The language

At this point, the best way to explore the current state of the language is
by perusing the `.lia` files in the `test` directory. The files with leading
numbers are part of the test suite and are representative of the current
state of the language.

