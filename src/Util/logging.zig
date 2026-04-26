// replaces Util/Logging.h
const std = @import("std");

pub const Level = enum(u3) {
    fatal = 0,
    err = 1,
    warn = 2,
    info = 3,
    trace = 4,
};

var current_level: Level = .fatal;

pub fn setLevel(level: Level) void {
    current_level = level;
}

pub fn getLevel() Level {
    return current_level;
}

pub fn shouldLog(level: Level) bool {
    return @intFromEnum(level) <= @intFromEnum(current_level);
}

fn levelPrefix(level: Level) []const u8 {
    return switch (level) {
        .fatal => "\x1b[31m[FATAL]\x1b[m ",
        .err   => "\x1b[91m[ERROR]\x1b[m ",
        .warn  => "\x1b[93m[WARN ]\x1b[m ",
        .info  => "\x1b[92m[INFO ]\x1b[m ",
        .trace => "[TRACE] ",
    };
}

pub fn log(level: Level, comptime fmt: []const u8, args: anytype) void {
    if (!shouldLog(level)) return;
    std.debug.print("{s}" ++ fmt ++ "\n", .{levelPrefix(level)} ++ args);
}

pub fn fatal(comptime fmt: []const u8, args: anytype) noreturn {
    log(.fatal, fmt, args);
    std.process.exit(1);
}

pub fn err(comptime fmt: []const u8, args: anytype) void {
    log(.err, fmt, args);
}

pub fn warn(comptime fmt: []const u8, args: anytype) void {
    log(.warn, fmt, args);
}

pub fn info(comptime fmt: []const u8, args: anytype) void {
    log(.info, fmt, args);
}

pub fn trace(comptime fmt: []const u8, args: anytype) void {
    log(.trace, fmt, args);
}
