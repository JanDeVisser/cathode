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


def test_compile(name, script):
    try:
        os.remove(name)
        os.remove("stdout")
        os.remove("stderr")
    except FileNotFoundError:
        pass

    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = [
            "../zig-out/bin/cathode",
            "--list",
            "--keep-assembly",
            "compile",
            name + ".cth",
        ]
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)

        error = []
        error.extend(check_stream(script, script["comptime_stdout"], out))
        error.extend(check_stream(script, script["comptime_stderr"], err))

    if len(error) == 0:
        if os.access(f"./{name}", os.R_OK or os.X_OK):
            print("\033[32m[  OK  ]\033[0m ", end="")
        else:
            print("\033[31m[FAILED]\033[0m \033[93m[  XX  ] [  XX  ]\033[0m")
            with open("stdout", "r") as out, open("stderr", "r") as err:
                print(out.read(), end="")
                print(err.read(), end="")
            return False
    else:
        print("\033[31m[FAILED]\033[0m \033[93m[  XX  ] [  XX  ]\033[0m")
        print(error)
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
                error.append(f"{name}: Exit code {ex} != {script['exit']}")
        error.extend(check_stream(script, script["stdout"], out))
        error.extend(check_stream(script, script["stderr"], err))

    os.remove("stdout")
    os.remove("stderr")
    if len(error) == 0:
        print("\033[32m[  OK  ]\033[0m ", end="")
    else:
        print("\033[31m[FAILED]\033[0m \033[93m[  XX  ]\033[0m")
        print(error)

    return len(error) == 0


def test_eval(name, script):
    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        # cmdline = [os.path.join(".compiled", name)]
        cmdline = ["../zig-out/bin/cathode", "eval", f"{name}.cth"]
        cmdline.extend(script["args"])
        ex = subprocess.call(cmdline, stdout=out, stderr=err)
        out.seek(0)
        err.seek(0)

        error = []
        if "exit" in script:
            expected = script["exit"]
            if ex != expected and ex != expected + 256 and ex != expected - 256:
                error.append(f"{name}: Exit code {ex} != {script['exit']}")
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


def test_script(name):
    if name.endswith(".cth"):
        name = name[:-4]

    with open(name + ".json") as fd:
        script = json.load(fd)

    print(f"{name:<25}", end="")
    ok = True
    if "no-compile" not in script:
        ok = test_compile(name, script)
    else:
        print("\033[93m[  XX  ] [  XX  ]\033[0m ", end="")
    if ok:
        if "no-eval" not in script:
            ok = test_eval(name, script)
        else:
            print("\033[93m[  XX  ]\033[0m ", end="\r\n")
    return ok


def run_tests(names, break_on_failure):
    print("                         Comptime Compiled   Eval")
    print("===================================================")
    for n in names:
        if not test_script(n) and break_on_failure:
            break


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
    if name.endswith(".cth"):
        name = name[:-4]
    # name = compile_script(name)
    if name is None:
        sys.exit(1)
    script = {"name": name}

    with open("stdout", "w+") as out, open("stderr", "w+") as err:
        cmdline = ["../zig-out/bin/cathode", "compile", name + ".cth"]
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
        for tests in [
            stripped
            for stripped in [test.strip() for test in fd]
            if not stripped.startswith("#")
        ]:
            name_args = filter(None, tests.split(" "))
            config_test(*name_args)


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
