const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Runtime C library (kept as C; see rt/cathode.h)
    const rt_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    rt_mod.addIncludePath(b.path("."));
    rt_mod.addCSourceFiles(.{
        .files = &.{
            "rt/dynarr.c",
            "rt/meta.c",
            "rt/puts.c",
            "rt/utf8.c",
        },
        .flags = &.{"-std=c23"},
    });

    // Architecture-specific trampoline (needed by the QBE VM interpreter)
    const resolved = target.result;
    const trampoline: ?[]const u8 = switch (resolved.os.tag) {
        .macos => switch (resolved.cpu.arch) {
            .aarch64 => "rt/arch/Darwin/arm64/trampoline.s",
            else => null,
        },
        .linux => switch (resolved.cpu.arch) {
            .x86_64 => "rt/arch/Linux/x86_64/trampoline.s",
            else => null,
        },
        else => null,
    };
    if (trampoline) |path| {
        rt_mod.addAssemblyFile(b.path(path));
    }

    const libart = b.addLibrary(.{
        .name = "cathodert",
        .linkage = .static,
        .root_module = rt_mod,
    });

    // Main compiler executable
    const exe_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    exe_mod.linkLibrary(libart);
    exe_mod.addIncludePath(b.path("."));

    const exe = b.addExecutable(.{
        .name = "cathode",
        .root_module = exe_mod,
    });
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    const run_step = b.step("run", "Run cathode");
    run_step.dependOn(&run_cmd.step);

    const test_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    test_mod.linkLibrary(libart);
    test_mod.addIncludePath(b.path("."));
    const unit_tests = b.addTest(.{
        .root_module = test_mod,
    });
    const run_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_tests.step);
}
