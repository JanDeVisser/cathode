// Concrete lexer for the cathode language.
// replaces the template Lexer<Types, Scanners...> from Util/Lexer.h
// with a single concrete implementation.
const std = @import("std");
const ArrayList = std.array_list.Managed;
const TokenLocation = @import("token_location.zig").TokenLocation;
const kw_mod = @import("../lang/keyword.zig");
const Keyword = kw_mod.Keyword;

pub const Radix = enum { decimal, hex, binary };

pub const QuoteType = enum(u8) {
    single = '\'',
    double = '"',
    backtick = '`',
};

pub const CommentKind = enum { line, block };

pub const TokenKind = enum {
    unknown,
    eof,
    eol,
    whitespace,
    identifier,
    keyword,
    number,
    string,
    comment,
    raw,
    symbol,
    // synthetic kinds set by the parser
    program,
    module,
};

pub const TokenValue = union(enum) {
    none: void,
    radix: Radix,
    quote: QuoteType,
    comment: CommentKind,
    keyword: Keyword,
    symbol: u8,
};

pub const Token = struct {
    kind: TokenKind = .unknown,
    location: TokenLocation = .{},
    value: TokenValue = .{ .none = {} },

    pub fn matchesSymbol(self: Token, ch: u8) bool {
        return self.kind == .symbol and self.value.symbol == ch;
    }

    pub fn matchesKeyword(self: Token, kw: Keyword) bool {
        return self.kind == .keyword and self.value.keyword == kw;
    }

    pub fn isIdentifier(self: Token) bool {
        return self.kind == .identifier;
    }

    pub fn isNumber(self: Token) bool {
        return self.kind == .number;
    }

    pub fn isEof(self: Token) bool {
        return self.kind == .eof;
    }
};

pub const LexerError = error{UnexpectedToken};

// ── Lexer ────────────────────────────────────────────────────────────────────

pub const Lexer = struct {
    allocator: std.mem.Allocator,
    sources: ArrayList(Source),
    lookback: ArrayList(Token),
    pushed_back: ArrayList(Token),
    current: ?Token = null,
    last_location: TokenLocation = .{},

    pub fn init(allocator: std.mem.Allocator) Lexer {
        return .{
            .allocator = allocator,
            .sources = ArrayList(Source).init(allocator),
            .lookback = ArrayList(Token).init(allocator),
            .pushed_back = ArrayList(Token).init(allocator),
        };
    }

    pub fn deinit(self: *Lexer) void {
        self.sources.deinit();
        self.lookback.deinit();
        self.pushed_back.deinit();
    }

    pub fn pushSource(self: *Lexer, buf: []const u8) !void {
        try self.sources.append(Source.init(buf));
    }

    pub fn tokenText(self: *Lexer, tok: Token) []const u8 {
        if (self.sources.items.len == 0) return "";
        const src = self.sources.items[self.sources.items.len - 1];
        const i = tok.location.index;
        const len = tok.location.length;
        if (i >= src.buf.len) return "";
        return src.buf[i..@min(i + len, src.buf.len)];
    }

    pub fn exhausted(self: *Lexer) bool {
        return self.sources.items.len == 0;
    }

    pub fn peek(self: *Lexer) Token {
        if (self.current) |c| return c;
        if (self.pushed_back.items.len > 0) {
            self.current = self.pushed_back.pop();
            return self.current.?;
        }
        if (self.sources.items.len == 0) {
            self.current = eofToken();
            return self.current.?;
        }
        // Keep scanning until we have a real token (skipping whitespace/comments)
        while (self.current == null) {
            const src = &self.sources.items[self.sources.items.len - 1];
            const result = src.next();
            switch (result) {
                .token => |tok| {
                    if (tok.kind == .eof) {
                        _ = self.sources.pop();
                        if (self.sources.items.len == 0) {
                            self.current = eofToken();
                        }
                        // if more sources remain, loop again to read from them
                    } else {
                        self.current = tok;
                    }
                },
                .skip => {}, // loop again
            }
        }
        return self.current.?;
    }

    pub fn lex(self: *Lexer) Token {
        const tok = self.peek();
        if (self.pushed_back.items.len > 0) {
            _ = self.pushed_back.pop();
        } else if (self.sources.items.len > 0) {
            self.sources.items[self.sources.items.len - 1].consume();
        }
        self.current = null;
        self.lookback.append(tok) catch {};
        self.last_location = tok.location;
        return tok;
    }

    pub const Bookmark = usize;

    pub fn bookmark(self: *Lexer) Bookmark {
        return self.lookback.items.len;
    }

    pub fn rewind(self: *Lexer, mark: Bookmark) void {
        while (self.lookback.items.len > mark) {
            if (self.lookback.pop()) |tok|
                self.pushed_back.append(tok) catch {};
        }
        self.current = null;
    }

    // Push a single token back to be re-read by the next peek/lex.
    pub fn pushToken(self: *Lexer, tok: Token) !void {
        try self.pushed_back.append(tok);
        if (self.lookback.items.len > 0) _ = self.lookback.pop();
        self.current = null;
    }

    // Lookback: 0 = most recently lexed token, 1 = one before that, etc.
    pub fn hasLookback(self: *const Lexer, n: usize) bool {
        return self.lookback.items.len > n;
    }

    pub fn lookbackAt(self: *const Lexer, n: usize) Token {
        std.debug.assert(self.lookback.items.len > n);
        return self.lookback.items[self.lookback.items.len - 1 - n];
    }

    pub fn nextMatches(self: *Lexer, kind: TokenKind) bool {
        return self.peek().kind == kind;
    }

    pub fn acceptNumber(self: *Lexer) ?Token {
        if (self.peek().kind == .number) return self.lex();
        return null;
    }

    pub fn expect(self: *Lexer, kind: TokenKind) LexerError!Token {
        const tok = self.peek();
        if (tok.kind != kind) {
            return error.UnexpectedToken;
        }
        return self.lex();
    }

    pub fn accept(self: *Lexer, kind: TokenKind) bool {
        if (self.peek().kind == kind) {
            _ = self.lex();
            return true;
        }
        return false;
    }

    pub fn expectKeyword(self: *Lexer, kw: Keyword) LexerError!void {
        const tok = self.peek();
        if (!tok.matchesKeyword(kw)) {
            return error.UnexpectedToken;
        }
        _ = self.lex();
    }

    pub fn acceptKeyword(self: *Lexer, kw: Keyword) bool {
        if (self.peek().matchesKeyword(kw)) {
            _ = self.lex();
            return true;
        }
        return false;
    }

    pub fn expectSymbol(self: *Lexer, ch: u8) LexerError!void {
        const tok = self.peek();
        if (!tok.matchesSymbol(ch)) {
            return error.UnexpectedToken;
        }
        _ = self.lex();
    }

    pub fn acceptSymbol(self: *Lexer, ch: u8) bool {
        if (self.peek().matchesSymbol(ch)) {
            _ = self.lex();
            return true;
        }
        return false;
    }

    pub fn expectIdentifier(self: *Lexer) LexerError!Token {
        const tok = self.peek();
        if (!tok.isIdentifier()) {
            return error.UnexpectedToken;
        }
        return self.lex();
    }

    pub fn acceptIdentifier(self: *Lexer) ?Token {
        if (self.peek().isIdentifier()) return self.lex();
        return null;
    }

    pub fn expectNumber(self: *Lexer) LexerError!Token {
        const tok = self.peek();
        if (!tok.isNumber()) {
            return error.UnexpectedToken;
        }
        return self.lex();
    }

    pub fn location(self: *Lexer) TokenLocation {
        if (self.sources.items.len == 0) return .{};
        return self.sources.items[self.sources.items.len - 1].loc;
    }
};

fn eofToken() Token {
    return .{ .kind = .eof };
}

// ── Source: single buffer being scanned ─────────────────────────────────────

const NextResult = union(enum) {
    token: Token,
    skip: void,
};

const Source = struct {
    buf: []const u8,
    index: usize = 0,
    loc: TokenLocation = .{},  // tracks line/column of the current position
    pending: ?Token = null,     // result of the last scan, not yet consumed

    fn init(buf: []const u8) Source {
        return .{ .buf = buf };
    }

    // Scan the next token (or indicate a skip).  Idempotent until consume().
    fn next(self: *Source) NextResult {
        if (self.pending) |tok| return .{ .token = tok };
        if (self.index >= self.buf.len) return .{ .token = eofToken() };

        const scan_start = self.index;
        var loc = self.loc;
        loc.length = 0;

        const raw = scanAt(self.buf, self.index);
        switch (raw) {
            .skip => |n| {
                self.advanceBy(n);
                return .{ .skip = {} };
            },
            .token => |tv| {
                var tok = tv.tok;
                loc.length = tv.len;
                tok.location = loc;
                self.index = scan_start + tv.len;
                // Update loc for the next call
                advanceLoc(&self.loc, self.buf, scan_start, self.index);
                self.loc.length = 0;
                self.pending = tok;
                return .{ .token = tok };
            },
        }
    }

    fn consume(self: *Source) void {
        self.pending = null;
    }

    fn advanceBy(self: *Source, n: usize) void {
        const start = self.index;
        self.index += n;
        advanceLoc(&self.loc, self.buf, start, self.index);
        self.loc.length = 0;
    }
};

fn advanceLoc(loc: *TokenLocation, buf: []const u8, from: usize, to: usize) void {
    var i = from;
    while (i < to and i < buf.len) : (i += 1) {
        if (buf[i] == '\n') {
            loc.line += 1;
            loc.column = 0;
        } else {
            loc.column += 1;
        }
        loc.index = i + 1;
    }
}

// ── Scanner ──────────────────────────────────────────────────────────────────

const TokenWithLen = struct { tok: Token, len: usize };
const RawResult = union(enum) {
    token: TokenWithLen,
    skip: usize, // number of bytes to skip
};

fn scanAt(buf: []const u8, i: usize) RawResult {
    if (i >= buf.len) return .{ .token = .{ .tok = eofToken(), .len = 0 } };
    const ch = buf[i];

    // Whitespace (skip)
    if (ch == ' ' or ch == '\t' or ch == '\n' or ch == '\r') {
        var j = i;
        while (j < buf.len and isWs(buf[j])) j += 1;
        return .{ .skip = j - i };
    }

    // // line comment (skip)
    if (ch == '/' and i + 1 < buf.len and buf[i + 1] == '/') {
        var j = i;
        while (j < buf.len and buf[j] != '\n') j += 1;
        return .{ .skip = j - i };
    }

    // /* block comment (skip) */
    if (ch == '/' and i + 1 < buf.len and buf[i + 1] == '*') {
        var j = i + 2;
        while (j + 1 < buf.len) : (j += 1) {
            if (buf[j] == '*' and buf[j + 1] == '/') return .{ .skip = j + 2 - i };
        }
        return .{ .skip = buf.len - i }; // unterminated block comment
    }

    // Number
    if (std.ascii.isDigit(ch)) return scanNumber(buf, i);

    // Quoted string
    if (ch == '"' or ch == '\'' or ch == '`') return scanString(buf, i);

    // Identifier / keyword
    if (std.ascii.isAlphabetic(ch) or ch == '_') return scanIdent(buf, i);

    // Try multi-character symbol keyword (e.g. ->, ==, &&, ::, <<, #::, @embed)
    return scanSymbolKeyword(buf, i);
}

// Scans the longest symbol-keyword starting at buf[start], falling back to
// a single-character symbol if no keyword matches.
fn scanSymbolKeyword(buf: []const u8, start: usize) RawResult {
    var len: usize = 1;
    var last_kw: ?kw_mod.Keyword = null;
    var last_len: usize = 0;
    while (start + len <= buf.len) {
        const slice = buf[start .. start + len];
        if (kw_mod.matchPrefix(slice) != null) {
            if (kw_mod.match(slice)) |kw| {
                last_kw = kw;
                last_len = len;
            }
            len += 1;
        } else break;
    }
    if (last_kw) |kw| {
        return .{ .token = .{
            .tok = .{ .kind = .keyword, .value = .{ .keyword = kw } },
            .len = last_len,
        } };
    }
    return .{ .token = .{
        .tok = .{ .kind = .symbol, .value = .{ .symbol = buf[start] } },
        .len = 1,
    } };
}

fn isWs(ch: u8) bool {
    return ch == ' ' or ch == '\t' or ch == '\n' or ch == '\r';
}

fn scanNumber(buf: []const u8, start: usize) RawResult {
    var i = start;
    var radix: Radix = .decimal;

    if (buf[i] == '0' and i + 1 < buf.len) {
        const nxt = buf[i + 1];
        if ((nxt == 'x' or nxt == 'X') and i + 2 < buf.len and std.ascii.isHex(buf[i + 2])) {
            radix = .hex;
            i += 2;
            while (i < buf.len and std.ascii.isHex(buf[i])) i += 1;
        } else if ((nxt == 'b' or nxt == 'B') and i + 2 < buf.len and isBin(buf[i + 2])) {
            radix = .binary;
            i += 2;
            while (i < buf.len and isBin(buf[i])) i += 1;
        } else {
            while (i < buf.len and std.ascii.isDigit(buf[i])) i += 1;
        }
    } else {
        while (i < buf.len and std.ascii.isDigit(buf[i])) i += 1;
    }

    return .{ .token = .{
        .tok = .{ .kind = .number, .value = .{ .radix = radix } },
        .len = i - start,
    } };
}

fn scanString(buf: []const u8, start: usize) RawResult {
    const q = buf[start];
    var i = start + 1;
    while (i < buf.len and buf[i] != q) {
        if (buf[i] == '\\') i += 1;
        i += 1;
    }
    if (i < buf.len) i += 1; // consume closing quote
    const qt: QuoteType = switch (q) {
        '\'' => .single,
        '"'  => .double,
        '`'  => .backtick,
        else => unreachable,
    };
    return .{ .token = .{
        .tok = .{ .kind = .string, .value = .{ .quote = qt } },
        .len = i - start,
    } };
}

fn scanIdent(buf: []const u8, start: usize) RawResult {
    var i = start;
    while (i < buf.len and (std.ascii.isAlphanumeric(buf[i]) or buf[i] == '_')) i += 1;
    const text = buf[start..i];

    if (kw_mod.match(text)) |kw| {
        return .{ .token = .{
            .tok = .{ .kind = .keyword, .value = .{ .keyword = kw } },
            .len = i - start,
        } };
    }

    return .{ .token = .{
        .tok = .{ .kind = .identifier },
        .len = i - start,
    } };
}

fn isBin(ch: u8) bool {
    return ch == '0' or ch == '1';
}
