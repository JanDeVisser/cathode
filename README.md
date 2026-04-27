# cathode

A post-C/C++ systems programming language.

## How to build

You need

- [zig|https://ziglang.org/download/] 0.16. I'm working with 0.16 right now.
- `python3` for the test runner.

```bash
$ cd ..../cathode
$ zig build
$ cd test
$ ./runtests.py all
```

## The language

At this point, the best way to explore the current state of the language is
by perusing the `.lia` files in the `test` directory. The files with leading
numbers are part of the test suite and are representative of the current
state of the language.
