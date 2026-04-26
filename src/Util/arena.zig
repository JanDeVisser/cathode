// replaces Util/Arena.h — memory arena + string interning
// Drops the UTF-32 twin entirely; all strings are UTF-8 slices.
const std = @import("std");

// String interning: deduplicates UTF-8 identifiers within a compilation.
// Returned slices remain valid for the lifetime of the StringPool.
pub const StringPool = struct {
    arena: std.heap.ArenaAllocator,
    map: std.StringHashMap([]const u8),

    pub fn init(backing: std.mem.Allocator) StringPool {
        return .{
            .arena = std.heap.ArenaAllocator.init(backing),
            .map = std.StringHashMap([]const u8).init(backing),
        };
    }

    pub fn deinit(self: *StringPool) void {
        self.map.deinit();
        self.arena.deinit();
    }

    // Returns a stable pointer to an interned copy of s.
    pub fn intern(self: *StringPool, s: []const u8) ![]const u8 {
        if (self.map.get(s)) |existing| return existing;
        const copy = try self.arena.allocator().dupe(u8, s);
        try self.map.put(copy, copy);
        return copy;
    }
};
