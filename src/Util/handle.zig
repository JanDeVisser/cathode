// replaces Ptr<T, Repo> from Util/Ptr.h
// Index-based stable handle into an ArrayList.  Avoids pointer invalidation
// when the list grows.  Null is represented by list == null.
const std = @import("std");
const ArrayList = std.array_list.Managed;

pub fn Handle(comptime T: type) type {
    return struct {
        list: ?*ArrayList(T) = null,
        index: usize = 0,

        const Self = @This();

        pub const null_handle: Self = .{ .list = null, .index = 0 };

        pub fn init(list: *ArrayList(T), index: usize) Self {
            return .{ .list = list, .index = index };
        }

        // Append a new T to the list and return a handle to it.
        pub fn append(list: *ArrayList(T), value: T) !Self {
            try list.append(value);
            return .{ .list = list, .index = list.items.len - 1 };
        }

        pub fn isNull(self: Self) bool {
            return self.list == null;
        }

        pub fn get(self: Self) *T {
            return &self.list.?.items[self.index];
        }

        pub fn getConst(self: Self) *const T {
            return &self.list.?.items[self.index];
        }

        pub fn eql(a: Self, b: Self) bool {
            if (a.list != b.list) return false;
            if (a.list == null) return true; // both null
            return a.index == b.index;
        }
    };
}
