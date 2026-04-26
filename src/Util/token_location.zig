// replaces TokenLocation from C++ Util/TokenLocation.h
pub const TokenLocation = struct {
    index: usize = 0,
    length: usize = 0,
    line: usize = 0,
    column: usize = 0,

    // Merge two locations into one spanning both
    pub fn merge(a: TokenLocation, b: TokenLocation) TokenLocation {
        const start = @min(a.index, b.index);
        const end = @max(a.index + a.length, b.index + b.length);
        return .{
            .index = start,
            .length = end - start,
            .line = @min(a.line, b.line),
            .column = @min(a.column, b.column),
        };
    }
};
