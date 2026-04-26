// replaces Lang/Keyword.h — keyword enum and match function
//
// Keywords are identified by the lexer.  Keyword.match() is called
// with the scanned identifier text and returns the matching keyword or null.

pub const Keyword = enum {
    // Assignment operators (two-character or longer symbols)
    assign_and,       // &=
    assign_decrement, // -=
    assign_divide,    // /=
    assign_increment, // +=
    assign_modulo,    // %=
    assign_multiply,  // *=
    assign_or,        // |=
    assign_shift_left,  // <<=
    assign_shift_right, // >>=
    assign_xor,       // ^=

    // Word keywords (alphabetic)
    alias,
    @"break",
    cast,        // ::
    @"const",
    @"continue",
    @"defer",
    @"else",
    embed,       // @embed
    @"enum",
    equals,      // ==
    @"error",
    @"export",
    @"extern",
    extern_link, // ->
    false,
    @"for",
    func,
    greater_equal, // >=
    @"if",
    import,
    include,     // @include
    less_equal,  // <=
    logical_and, // &&
    logical_or,  // ||
    loop,
    must,
    not_equal,   // !=
    null,
    public,
    range,       // ..
    @"return",
    shift_left,  // <<
    shift_right, // >>
    sizeof,      // #::
    @"struct",
    @"switch",
    switch_case, // =>
    true,
    @"while",
    yield,
};

// Maps keyword enum values to their source text representations
const keyword_strings = [_]struct { kw: Keyword, text: []const u8 }{
    .{ .kw = .assign_and,          .text = "&=" },
    .{ .kw = .assign_decrement,    .text = "-=" },
    .{ .kw = .assign_divide,       .text = "/=" },
    .{ .kw = .assign_increment,    .text = "+=" },
    .{ .kw = .assign_modulo,       .text = "%=" },
    .{ .kw = .assign_multiply,     .text = "*=" },
    .{ .kw = .assign_or,           .text = "|=" },
    .{ .kw = .assign_shift_left,   .text = "<<=" },
    .{ .kw = .assign_shift_right,  .text = ">>=" },
    .{ .kw = .assign_xor,          .text = "^=" },
    .{ .kw = .alias,               .text = "alias" },
    .{ .kw = .@"break",            .text = "break" },
    .{ .kw = .cast,                .text = "::" },
    .{ .kw = .@"const",            .text = "const" },
    .{ .kw = .@"continue",         .text = "continue" },
    .{ .kw = .@"defer",            .text = "defer" },
    .{ .kw = .@"else",             .text = "else" },
    .{ .kw = .embed,               .text = "@embed" },
    .{ .kw = .@"enum",             .text = "enum" },
    .{ .kw = .equals,              .text = "==" },
    .{ .kw = .@"error",            .text = "error" },
    .{ .kw = .@"export",           .text = "export" },
    .{ .kw = .@"extern",           .text = "extern" },
    .{ .kw = .extern_link,         .text = "->" },
    .{ .kw = .false,               .text = "false" },
    .{ .kw = .@"for",              .text = "for" },
    .{ .kw = .func,                .text = "func" },
    .{ .kw = .greater_equal,       .text = ">=" },
    .{ .kw = .@"if",               .text = "if" },
    .{ .kw = .import,              .text = "import" },
    .{ .kw = .include,             .text = "@include" },
    .{ .kw = .less_equal,          .text = "<=" },
    .{ .kw = .logical_and,         .text = "&&" },
    .{ .kw = .logical_or,          .text = "||" },
    .{ .kw = .loop,                .text = "loop" },
    .{ .kw = .must,                .text = "must" },
    .{ .kw = .not_equal,           .text = "!=" },
    .{ .kw = .null,                .text = "null" },
    .{ .kw = .public,              .text = "public" },
    .{ .kw = .range,               .text = ".." },
    .{ .kw = .@"return",           .text = "return" },
    .{ .kw = .shift_left,          .text = "<<" },
    .{ .kw = .shift_right,         .text = ">>" },
    .{ .kw = .sizeof,              .text = "#::" },
    .{ .kw = .@"struct",           .text = "struct" },
    .{ .kw = .@"switch",           .text = "switch" },
    .{ .kw = .switch_case,         .text = "=>" },
    .{ .kw = .true,                .text = "true" },
    .{ .kw = .@"while",            .text = "while" },
    .{ .kw = .yield,               .text = "yield" },
};

// Returns the source text for a keyword
pub fn text(kw: Keyword) []const u8 {
    for (keyword_strings) |ks| {
        if (ks.kw == kw) return ks.text;
    }
    unreachable;
}

// Used by the lexer identifier scanner to check whether a word is a keyword.
// Returns the matching keyword or null.  The match must be exact.
pub fn match(s: []const u8) ?Keyword {
    for (keyword_strings) |ks| {
        if (std.mem.eql(u8, ks.text, s)) return ks.kw;
    }
    return null;
}

// Prefix match: returns a keyword if s is a prefix of any keyword text,
// used by the symbol-keyword scanner.
pub fn matchPrefix(s: []const u8) ?Keyword {
    for (keyword_strings) |ks| {
        if (std.mem.startsWith(u8, ks.text, s)) return ks.kw;
    }
    return null;
}

const std = @import("std");
