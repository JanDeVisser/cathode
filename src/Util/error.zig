// replaces Util/Error.h — error types used throughout the compiler
const std = @import("std");

// IO/OS errors wrapping errno, replaces LibCError
pub const OsError = struct {
    code: std.posix.E,
    description: []const u8,

    pub fn fromErrno() OsError {
        const e = std.posix.errno(-1); // captures last errno
        return .{ .code = e, .description = @tagName(e) };
    }

    pub fn custom(description: []const u8) OsError {
        return .{ .code = .SUCCESS, .description = description };
    }

    pub fn format(
        self: OsError,
        comptime _: []const u8,
        _: std.fmt.FormatOptions,
        writer: anytype,
    ) !void {
        try writer.print("{s} ({})", .{ self.description, @intFromEnum(self.code) });
    }
};

// Compiler errors carry a source location + message
pub const CompileError = struct {
    line: usize,
    column: usize,
    message: []const u8,
};

// Error set for IO operations
pub const IoError = error{
    FileNotFound,
    ReadFailed,
    WriteFailed,
    ProcessFailed,
};
