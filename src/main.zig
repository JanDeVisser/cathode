// cathode compiler — main entry point
const std = @import("std");
const ArrayList = std.array_list.Managed;
const Allocator = std.mem.Allocator;
const Io = std.Io;

const type_mod  = @import("lang/type.zig");
const sn        = @import("lang/syntax_node.zig");
const AstNode   = sn.AstNode;
const parser_mod = @import("lang/parser.zig");
const Parser    = parser_mod.Parser;
const ast_mod   = @import("lang/ast.zig");
const qbe_mod   = @import("lang/qbe.zig");
const config    = @import("lang/config.zig");

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const io  = init.io;

    try type_mod.init(gpa);
    defer type_mod.deinit();

    var args_iter = std.process.Args.iterate(init.minimal.args);
    _ = args_iter.skip(); // skip argv[0]

    // ── Collect raw args ──────────────────────────────────────────────────────
    var raw = ArrayList([]const u8).init(gpa);
    defer raw.deinit();
    while (args_iter.next()) |a| try raw.append(a);

    // ── Parse flags, command, file args ──────────────────────────────────────
    var stop_after_parse    = false;
    var stop_after_analysis = false;
    var cmd: ?[]const u8    = null;
    var file_args = ArrayList([]const u8).init(gpa);
    defer file_args.deinit();

    var idx: usize = 0;
    while (idx < raw.items.len) : (idx += 1) {
        const a = raw.items[idx];
        if (eql(a, "--")) { idx += 1; break; }
        if (std.mem.startsWith(u8, a, "--")) {
            const name = a[2..];
            if (eql(name, "stop-after-parse"))    stop_after_parse = true
            else if (eql(name, "stop-after-analysis")) stop_after_analysis = true;
            // --trace / --verbose / --list / --keep-* are accepted and ignored for now
        } else if (cmd == null) {
            cmd = a;
        } else {
            try file_args.append(a);
        }
    }
    while (idx < raw.items.len) : (idx += 1) try file_args.append(raw.items[idx]);

    const c = cmd orelse {
        try printUsage(io);
        std.process.exit(1);
    };

    if (eql(c, "help") or eql(c, "--help") or eql(c, "-h")) {
        try printUsage(io);
        return;
    }
    if (eql(c, "version") or eql(c, "--version") or eql(c, "-v")) {
        try printVersion(io);
        return;
    }

    // ── build: gather all .cth files in the current directory ────────────────
    if (eql(c, "build")) {
        var dir = try Io.Dir.cwd().openDir(io, ".", .{ .iterate = true });
        defer dir.close(io);
        var it = dir.iterate();
        while (try it.next(io)) |entry| {
            if (entry.kind != .file) continue;
            if (!std.mem.endsWith(u8, entry.name, ".cth")) continue;
            try file_args.append(try gpa.dupe(u8, entry.name));
        }
    } else if (!eql(c, "compile")) {
        try printUsage(io);
        std.process.exit(1);
    }

    if (file_args.items.len == 0) {
        try printUsage(io);
        std.process.exit(1);
    }

    // ── Derive program name from first source file ────────────────────────────
    const prog_name = stemOf(file_args.items[0]);

    // ── Discover installation root ────────────────────────────────────────────
    const cathode_root = config.cathodeDir(io, gpa) catch {
        std.process.exit(1);
    };
    defer gpa.free(cathode_root);

    const lib_dir = try config.libDir(io, cathode_root, gpa);
    defer gpa.free(lib_dir);

    // ── Run the pipeline ──────────────────────────────────────────────────────
    var parser = Parser.init(gpa);
    defer parser.deinit();

    // 1. Parse
    const parse_ok = parseAll(&parser, gpa, io, cathode_root, file_args.items) catch |err| {
        std.debug.print("cathode: parse error: {}\n", .{err});
        std.process.exit(1);
    };
    if (!parse_ok) std.process.exit(1);
    if (stop_after_parse) return;

    // 2. Normalize + Bind
    ast_mod.bindAll(&parser) catch |err| {
        printErrors(parser.errors.items);
        std.debug.print("cathode: semantic analysis failed: {}\n", .{err});
        std.process.exit(1);
    };
    if (parser.errors.items.len > 0) {
        printErrors(parser.errors.items);
        std.process.exit(1);
    }
    if (stop_after_analysis) return;

    // 3. QBE codegen + compile + link
    qbe_mod.compileQbe(gpa, io, &parser, parser.program, prog_name, lib_dir) catch |err| {
        std.debug.print("cathode: compilation failed: {}\n", .{err});
        std.process.exit(1);
    };
}

// ── Parse stdlib + user files ─────────────────────────────────────────────────

fn parseAll(
    parser: *Parser,
    gpa: Allocator,
    io: Io,
    cathode_root: []const u8,
    source_files: []const []const u8,
) !bool {
    // Load and parse std.cth as the Program (establishes the base namespace)
    const std_path = try std.fmt.allocPrint(gpa, "{s}/share/std.cth", .{cathode_root});
    defer gpa.free(std_path);

    const std_text = Io.Dir.cwd().readFileAlloc(io, std_path, gpa, .unlimited) catch |err| {
        std.debug.print("cathode: cannot read {s}: {}\n", .{ std_path, err });
        return false;
    };
    defer gpa.free(std_text);

    _ = try parser.parseProgram("std", std_text) orelse {
        printErrors(parser.errors.items);
        return false;
    };

    // Load and parse each user source file as a Module, then attach to the program
    for (source_files) |path| {
        const text = Io.Dir.cwd().readFileAlloc(io, path, gpa, .unlimited) catch |err| {
            std.debug.print("cathode: cannot read {s}: {}\n", .{ path, err });
            return false;
        };
        defer gpa.free(text);

        const mod_node = try parser.parseModule(stemOf(path), text) orelse continue;

        // Append module node to program.statements
        const prog = parser.program.get();
        const old  = prog.node.program.statements;
        const new  = try parser.arena.allocator().alloc(AstNode, old.len + 1);
        @memcpy(new[0..old.len], old);
        new[old.len] = mod_node;
        prog.node.program.statements = new;
    }

    if (parser.errors.items.len > 0) {
        printErrors(parser.errors.items);
        return false;
    }
    return true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

fn printErrors(errors: []const parser_mod.ParseError) void {
    for (errors) |err| {
        std.debug.print("{d}:{d}: {s}\n", .{
            err.location.line + 1,
            err.location.column + 1,
            err.message,
        });
    }
}

fn stemOf(path: []const u8) []const u8 {
    const base = std.fs.path.basename(path);
    const dot = std.mem.lastIndexOfScalar(u8, base, '.') orelse return base;
    return base[0..dot];
}

fn eql(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

fn printUsage(io: Io) !void {
    try Io.File.writeStreamingAll(Io.File.stdout(), io, usage_text);
}

fn printVersion(io: Io) !void {
    try Io.File.writeStreamingAll(Io.File.stdout(), io, "cathode 0.0.1\n");
}

const usage_text =
    \\cathode - cathode language compiler
    \\
    \\Usage:
    \\  cathode help | --help | -h         This text
    \\  cathode version | --version | -v   Display version
    \\  cathode [OPTIONS] build            Compile all .cth files in the current directory
    \\  cathode [OPTIONS] compile <file> . Compile the given source files
    \\
    \\Options:
    \\  --stop-after-parse      Stop after syntactic parsing
    \\  --stop-after-analysis   Stop after semantic analysis (normalize + bind)
    \\  --trace                 Print debug tracing
    \\  --verbose               Progress information
    \\  --list                  List generated QBE IL to stderr
    \\
;
