// Discovers the cathode installation root and library paths.
// Checks CATHODE_DIR env var first, then derives from executable location.
const std = @import("std");
const Io = std.Io;
const Allocator = std.mem.Allocator;

fn fileExists(io: Io, path: []const u8) bool {
    _ = std.Io.Dir.cwd().statFile(io, path, .{}) catch return false;
    return true;
}

// Returns an owned slice with the cathode root directory.
// The caller must free it.
pub fn cathodeDir(io: Io, alloc: Allocator) ![]const u8 {
    if (std.c.getenv("CATHODE_DIR")) |dir| {
        return alloc.dupe(u8, std.mem.span(dir));
    }

    // Derive from exe path.  We try:
    //   <exe_dir>/../share/std.cth   (standard install: /usr/local/bin → /usr/local)
    //   <exe_dir>/../../share/std.cth  (zig-out/bin/cathode → project root)
    const exe = try std.process.executablePathAlloc(io, alloc);
    defer alloc.free(exe);

    const bin_dir = std.fs.path.dirname(exe) orelse return error.NotFound;
    // One level up (e.g. /usr/local from /usr/local/bin)
    if (std.fs.path.dirname(bin_dir)) |prefix| {
        const probe = try std.fmt.allocPrint(alloc, "{s}/share/std.cth", .{prefix});
        defer alloc.free(probe);
        if (fileExists(io, probe)) return alloc.dupe(u8, prefix);

        // Two levels up (e.g. project root from zig-out/bin)
        if (std.fs.path.dirname(prefix)) |project| {
            const probe2 = try std.fmt.allocPrint(alloc, "{s}/share/std.cth", .{project});
            defer alloc.free(probe2);
            if (fileExists(io, probe2)) return alloc.dupe(u8, project);
        }
    }

    std.debug.print(
        "cathode: cannot locate installation directory.\n" ++
        "Set the CATHODE_DIR environment variable to the cathode root.\n", .{});
    return error.NotFound;
}

// Returns the directory that contains libcathodert.a.
pub fn libDir(io: Io, cathode_root: []const u8, alloc: Allocator) ![]const u8 {
    // zig-out layout (development build)
    const dev_lib = try std.fmt.allocPrint(alloc, "{s}/zig-out/lib", .{cathode_root});
    if (fileExists(io, dev_lib)) return dev_lib;
    alloc.free(dev_lib);
    // Standard installed layout
    return std.fmt.allocPrint(alloc, "{s}/lib", .{cathode_root});
}
