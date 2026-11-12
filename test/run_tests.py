#!/usr/bin/env python3
#  Copyright (c) 2021-2025, Jan de Visser <jan@finiandarcy.com>
#
#  SPDX-License-Identifier: MIT

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

BUILD_DIR = "../build"
CATHODE = f"{BUILD_DIR}/bin/cathode"

def print_file(filename, banner):
    if os.access(filename, os.R_OK or os.X_OK):
        print(f"{banner}:")
        print("======================================")
        with open(filename, "r") as fd:
            lines = [line for line in fd]
            for num, line in zip(range(len(lines)), lines):
                print(f"{num:3} | {line}", end="")
        print("======================================")


def check_stream(script, what, stream):
    ret = []
    written = [line.strip() for line in stream]
    written = "\n".join(written)
    expected = (
        "\n".join(what)
        if isinstance(what, (list, tuple))
        else ""
        if what is None
        else what
    )
    if written != expected and not re.match(
        f"(\\[.*Fatal.*\\]\\ )?{expected}", written
    ):
        ret.append(f"{script['name']}: '{written}' != '{expected}'")
    return ret


def test_compile(name, script, compiler_output):
    try:
        os.remove(name)
        os.remove("stdout")
        os.remove("stderr")
        os.remove(f".cathode/{name}.ssa")
        os.remove(f".cathode/{name}.s")
        os.remove(f".cathode/{name}.out")
    except FileNotFoundError:
        pass

    output = []

    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = [
            CATHODE,
            "--verbose",
            "--dump-trees",
            "--keep-assembly",
            "compile",
            f"{name}.cth",
        ]
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)

        error = []
        error.extend(check_stream(script, script["comptime_stdout"], out))
        error.extend(check_stream(script, script["comptime_stderr"], err))

        out.seek(0)
        err.seek(0)
        output.extend([line for line in out])
        output.extend([line for line in err])

    if len(error) == 0:
        if os.access(f"./{name}", os.R_OK or os.X_OK):
            print("\033[32m[  OK  ]\033[0m ", end="")
        else:
            print("\033[31m[FAILED]\033[0m \033[93m[  XX  ] [  XX  ]\033[0m")
            if compiler_output:
                for line in output:
                    print(line, end="")
            return False
    else:
        print("\033[31m[FAILED]\033[0m \033[93m[  XX  ] [  XX  ]\033[0m")
        if compiler_output:
            for line in output:
                print(line, end="")
        return False

    os.remove("stdout")
    os.remove("stderr")
    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = [f"./{name}"]
        cmdline.extend(script["args"])
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)

        error = []
        if "exit" in script:
            expected = script["exit"]
            if ex != expected and ex != expected + 256 and ex != expected - 256:
                error.append(f"{name}: Exit code {ex}, expected {script['exit']}")
        error.extend(check_stream(script, script["stdout"], out))
        error.extend(check_stream(script, script["stderr"], err))

        out.seek(0)
        err.seek(0)
        output.extend([line for line in out])
        output.extend([line for line in err])

    os.remove("stdout")
    os.remove("stderr")
    if len(error) == 0:
        print("\033[32m[  OK  ]\033[0m ", end="")
    else:
        print("\033[31m[FAILED]\033[0m \033[93m[  XX  ]\033[0m")

    if compiler_output:
        with open(f".cathode/{name}.out", "w+") as fd:
            fd.writelines(output)
    return len(error) == 0


def test_eval(name, script):
    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = [CATHODE, "eval", f"{name}.cth"]
        cmdline.extend(script["args"])
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)

        error = []
        if "exit" in script:
            expected = script["exit"]
            if ex != expected and ex != expected + 256 and ex != expected - 256:
                error.append(f"{name}: Eval Exit code {ex}, expected {script['exit']}")
        o = script["comptime_stdout"]
        o.extend(script["stdout"])
        error.extend(check_stream(script, o, out))
        e = script["comptime_stderr"]
        e.extend(script["stderr"])
        error.extend(check_stream(script, e, err))

    os.remove("stdout")
    os.remove("stderr")
    if len(error) == 0:
        print("\033[32m[  OK  ]\033[0m ", end="\r\n")
    else:
        print("\033[31m[FAILED]\033[0m ", end="\r\n")
        print(error)

    return len(error) == 0


def test_script(name, compiler_output=False):
    name = name.removesuffix(".cth")

    with open(name + ".json") as fd:
        script = json.load(fd)

    print(f"{name:<25}", end="")
    ok = True
    if "no-compile" not in script:
        ok = test_compile(name, script, compiler_output)
    else:
        print("\033[93m[  XX  ] [  XX  ]\033[0m ", end="")
    if ok:
        if "no-eval" not in script:
            ok = test_eval(name, script)
        else:
            print("\033[93m[  XX  ]\033[0m ", end="\r\n")
    if compiler_output:
        print_file(f"{name}.cth", f"Test program {name}.cth")
        print_file(f".cathode/{name}.out", "Compiler output")
        print_file(f".cathode/{name}.ssa", "QBE program")

    return ok


def run_tests(names, break_on_failure):
    print("                         Comptime Compiled   Eval")
    print("===================================================")
    for n in names:
        if not test_script(n, False) and break_on_failure:
            break


def run_one_test(name, verbose=False):
    print("                         Comptime Compiled   Eval")
    print("===================================================")
    test_script(name, verbose)


def load_test_names():
    if os.path.exists("tests.json"):
        with open("tests.json") as fd:
            scripts = json.load(fd)
    else:
        scripts = []
    return scripts


def run_all_tests(break_on_failure):
    run_tests(load_test_names(), break_on_failure)


def config_test(name, *args):
    name = name.removesuffix(".cth")

    # name = compile_script(name)
    if name is None:
        sys.exit(1)
    script = {"name": name}

    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        cmdline = ["../build/bin/cathode", "compile", name + ".cth"]
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)
        script["comptime_stdout"] = [line.strip() for line in out]
        script["comptime_stderr"] = [line.strip() for line in err]

    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        cmdline = [f"./{name}"]
        cmdline.extend(args)
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)
        script["stdout"] = [line.strip() for line in out]
        script["stderr"] = [line.strip() for line in err]
        script["exit"] = ex
        script["args"] = args

    scripts = load_test_names()
    if name not in scripts:
        scripts.append(name)
        with open("tests.json", "w") as fd:
            json.dump(scripts, fd, indent=2)
            print(file=fd)
    with open(f"{name}.json", "w") as fd:
        json.dump(script, fd, indent=2)
        print(file=fd)
    os.remove("stdout")
    os.remove("stderr")


def remove_test(name, destroy=False):
    if destroy:
        fname = name + ".json"
        os.path.exists(fname) and os.remove(fname)
    scripts = load_test_names()
    if name in scripts:
        scripts.remove(name)
        with open("tests.json", "w+") as fd:
            json.dump(scripts, fd, indent=2)


def remove_all_tests(nuke=False):
    scripts = load_test_names()
    if nuke:
        for script in scripts:
            fname = script + ".json"
            os.path.exists(fname) and os.remove(fname)
    if os.path.exists("tests.json"):
        os.remove("tests.json")


def print_index():
    scripts = load_test_names()
    for script in scripts:
        s = [script]
        with open(script + ".json") as fd:
            script_data = json.load(fd)
            if "args" in script_data:
                s.extend(script_data["args"])
        print(" ".join(s))


def config_tests(tests):
    with open(tests) as fd:
        # FIXME Why does this map() not work?
        # map(config_test, [stripped for stripped in [name.strip() for name in fd] if not stripped.startswith("#")])
        for test_name in [
            stripped
            for stripped in [test.strip() for test in fd]
            if not stripped.startswith("#")
        ]:
            name_args = filter(None, test_name.split(" "))
            config_test(*name_args)

def build_compiler():
    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = [
            "cmake",
            "--build",
            BUILD_DIR,
            "--target",
            "install",
        ]
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)
    if ex == 0:
        run_one_test("01_helloworld", False)
    else:
        print("Compilation failed!\n")
        with open("stdout", "r") as err:
            for line in err:
                print(line, end="")

def initialize():
    shutil.rmtree(".compiled", True)
    os.mkdir(".compiled")


initialize()

arg_parser = argparse.ArgumentParser(
    prog="run_tests", description="cathode language test runner"
)

subparsers = arg_parser.add_subparsers(
    title="Commands",
    required=True,
    dest="cmd",
)

index_tests = subparsers.add_parser(
    "index",
    help="Displays an index of all registered tests. Output can be used by run_tests add-all",
)

make_all = subparsers.add_parser(
    "make",
    help="Builds the cathode compiler",
)

run_all = subparsers.add_parser(
    "all",
    help="Execute all tests in the registry",
)

run_all.add_argument(
    "-b",
    "--break-on-failure",
    action="store_true",
    help="Break on test failure",
)

run_one = subparsers.add_parser(
    "one",
    help="Execute one single test, dumping compiler output on failure",
)

run_one.add_argument(
    "test",
    nargs=1,
    help="Test to run",
)

run_one.add_argument(
    "-v",
    "--verbose",
    action="store_true",
    help="Show compiler logging output",
)

run_some = subparsers.add_parser(
    "some",
    help="Execute the specified tests",
)

run_some.add_argument(
    "-b",
    "--break-on-failure",
    action="store_true",
    help="Break on test failure",
)

run_some.add_argument(
    "test",
    nargs="+",
    help="Test(s) to run",
)

create_tests = subparsers.add_parser(
    "create",
    help="Add the specified test script with optional arguments to the test registry, and executes them",
)

create_tests.add_argument(
    "test",
    nargs="+",
    help="Test(s) to create",
)

add_all = subparsers.add_parser(
    "add-all",
    help="Add all tests in <File>. <File> should contain test script names, one per line",
)

add_all.add_argument(
    "tests",
    nargs=1,
    help="File containing tests to create. One per line",
)

delete_tests = subparsers.add_parser(
    "delete",
    help="Remove the specified tests from the registry. The expected outcome .json files will be retained",
)

delete_tests.add_argument(
    "test",
    nargs="+",
    help="Test(s) to delete",
)

destroy_tests = subparsers.add_parser(
    "destroy",
    help="Remove the specified tests from the registry. The expected outcome .json files will be deleted as well",
)

destroy_tests.add_argument(
    "test",
    nargs="+",
    help="Test(s) to destroy",
)

clear_reg = subparsers.add_parser(
    "clear",
    help="Clear the test registry. The expected outcome .json files will be retained",
)

nuke = subparsers.add_parser(
    "nuke",
    help="Clear the test registry. The expected outcome .json files will be deleted as well",
)

args = arg_parser.parse_args()

if args.cmd == "all":
    run_all_tests(args.break_on_failure)
if args.cmd == "make":
    build_compiler()
if args.cmd == "one":
    run_one_test(args.test[0], args.verbose)
if args.cmd == "some":
    run_tests(args.test, args.break_on_failure)
if args.cmd == "create":
    config_test(args.test[0], *args.test[1:])
if args.cmd == "add-all":
    config_tests(args.tests)
if args.cmd == "delete":
    for name in args.test:
        remove_test(name)
if args.cmd == "destroy":
    for name in args.test:
        remove_test(name, True)
if args.cmd == "clear" or args.cmd == "nuke":
    remove_all_tests(args.cmd == "nuke")
if args.cmd == "index":
    print_index()
