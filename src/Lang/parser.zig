// replaces Lang/Parser.h + Lang/Parser.cpp
const std = @import("std");
const ArrayList = std.array_list.Managed;
const TokenLocation = @import("../Util/token_location.zig").TokenLocation;
const Lexer = @import("../Util/lexer.zig").Lexer;
const Token = @import("../Util/lexer.zig").Token;
const TokenKind = @import("../Util/lexer.zig").TokenKind;
const Radix = @import("../Util/lexer.zig").Radix;
const QuoteType = @import("../Util/lexer.zig").QuoteType;
const Keyword = @import("keyword.zig").Keyword;
const op_mod = @import("operator.zig");
const Operator = op_mod.Operator;
const Position = op_mod.Position;
const Associativity = op_mod.Associativity;
const sn = @import("syntax_node.zig");
const AstNode = sn.AstNode;
const AstNodeImpl = sn.AstNodeImpl;
const SyntaxNode = sn.SyntaxNode;
const NsHandle = sn.NsHandle;
const Namespace = sn.Namespace;
const Label = sn.Label;
const Visibility = sn.Visibility;
const type_mod = @import("type.zig");
const TypeHandle = type_mod.TypeHandle;
const BindResult = sn.BindResult;

pub const Precedence = i32;

pub const ParseLevel = enum { module, function, block };

pub const ParseError = struct {
    location: TokenLocation,
    message: []const u8,
};

// Token carries either a single-char symbol or a keyword.
pub const OperatorSym = union(enum) {
    symbol: u8,
    keyword: Keyword,
};

pub const OperatorDef = struct {
    op: Operator,
    sym: OperatorSym,
    precedence: Precedence,
    position: Position = .infix,
    associativity: Associativity = .left,
};

const BindingPower = struct { left: i32, right: i32 };

fn bindingPower(def: OperatorDef) BindingPower {
    return switch (def.position) {
        .infix => switch (def.associativity) {
            .left  => .{ .left = def.precedence * 2 - 1, .right = def.precedence * 2 },
            .right => .{ .left = def.precedence * 2,     .right = def.precedence * 2 - 1 },
        },
        .prefix  => .{ .left = -1, .right = def.precedence * 2 - 1 },
        .postfix => .{ .left = def.precedence * 2 - 1, .right = -1 },
        .closing => .{ .left = -1, .right = -1 },
    };
}

// ── Operator table ────────────────────────────────────────────────────────────
// Matches the static initializer in Parser.cpp exactly.
const operator_table = [_]OperatorDef{
    .{ .op = .add,               .sym = .{ .symbol  = '+' },             .precedence = 11 },
    .{ .op = .address_of,        .sym = .{ .symbol  = '&' },             .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .assign,            .sym = .{ .symbol  = '=' },             .precedence = 2,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_and,        .sym = .{ .keyword = .assign_and },     .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_decrement,  .sym = .{ .keyword = .assign_decrement},.precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_divide,     .sym = .{ .keyword = .assign_divide },  .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_increment,  .sym = .{ .keyword = .assign_increment},.precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_modulo,     .sym = .{ .keyword = .assign_modulo },  .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_multiply,   .sym = .{ .keyword = .assign_multiply}, .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_or,         .sym = .{ .keyword = .assign_or },      .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .assign_shift_left, .sym = .{ .keyword = .assign_shift_left},.precedence = 1, .position = .infix,   .associativity = .right },
    .{ .op = .assign_shift_right,.sym = .{ .keyword = .assign_shift_right},.precedence= 1, .position = .infix,   .associativity = .right },
    .{ .op = .assign_xor,        .sym = .{ .keyword = .assign_xor },     .precedence = 1,  .position = .infix,   .associativity = .right },
    .{ .op = .binary_invert,     .sym = .{ .symbol  = '~' },             .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .call,              .sym = .{ .symbol  = '(' },             .precedence = 15 },
    .{ .op = .call,              .sym = .{ .symbol  = ')' },             .precedence = 15, .position = .closing },
    .{ .op = .cast,              .sym = .{ .keyword = .cast },           .precedence = 14 },
    .{ .op = .divide,            .sym = .{ .symbol  = '/' },             .precedence = 12 },
    .{ .op = .equals,            .sym = .{ .keyword = .equals },         .precedence = 8 },
    .{ .op = .greater,           .sym = .{ .symbol  = '>' },             .precedence = 8 },
    .{ .op = .greater_equal,     .sym = .{ .keyword = .greater_equal },  .precedence = 8 },
    .{ .op = .idempotent,        .sym = .{ .symbol  = '+' },             .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .length,            .sym = .{ .symbol  = '#' },             .precedence = 9,  .position = .prefix,  .associativity = .right },
    .{ .op = .less,              .sym = .{ .symbol  = '<' },             .precedence = 8 },
    .{ .op = .less_equal,        .sym = .{ .keyword = .less_equal },     .precedence = 8 },
    .{ .op = .logical_and,       .sym = .{ .keyword = .logical_and },    .precedence = 5 },
    .{ .op = .logical_invert,    .sym = .{ .symbol  = '!' },             .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .logical_or,        .sym = .{ .keyword = .logical_or },     .precedence = 4 },
    .{ .op = .member_access,     .sym = .{ .symbol  = '.' },             .precedence = 15 },
    .{ .op = .modulo,            .sym = .{ .symbol  = '%' },             .precedence = 12 },
    .{ .op = .multiply,          .sym = .{ .symbol  = '*' },             .precedence = 12 },
    .{ .op = .negate,            .sym = .{ .symbol  = '-' },             .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .not_equal,         .sym = .{ .keyword = .not_equal },      .precedence = 8 },
    .{ .op = .range,             .sym = .{ .keyword = .range },          .precedence = 2 },
    .{ .op = .sequence,          .sym = .{ .symbol  = ',' },             .precedence = 1 },
    .{ .op = .shift_left,        .sym = .{ .keyword = .shift_left },     .precedence = 10 },
    .{ .op = .shift_right,       .sym = .{ .keyword = .shift_right },    .precedence = 10 },
    .{ .op = .sizeof,            .sym = .{ .keyword = .sizeof },         .precedence = 9,  .position = .prefix,  .associativity = .right },
    .{ .op = .subscript,         .sym = .{ .symbol  = '[' },             .precedence = 15, .position = .postfix },
    .{ .op = .subscript,         .sym = .{ .symbol  = ']' },             .precedence = 15, .position = .closing },
    .{ .op = .subtract,          .sym = .{ .symbol  = '-' },             .precedence = 11 },
    .{ .op = .unwrap,            .sym = .{ .keyword = .must },           .precedence = 14, .position = .prefix,  .associativity = .right },
    .{ .op = .unwrap_error,      .sym = .{ .keyword = .@"error" },       .precedence = 14, .position = .prefix,  .associativity = .right },
};

fn symMatches(sym: OperatorSym, tok: Token) bool {
    return switch (sym) {
        .symbol  => |c|  tok.kind == .symbol  and tok.value.symbol  == c,
        .keyword => |kw| tok.kind == .keyword and tok.value.keyword == kw,
    };
}

// ── Parser ────────────────────────────────────────────────────────────────────

pub const Parser = struct {
    gpa: std.mem.Allocator,
    arena: std.heap.ArenaAllocator,
    nodes: ArrayList(AstNodeImpl),
    namespace_nodes: ArrayList(Namespace),
    namespaces: ArrayList(NsHandle),
    lexer: Lexer,
    level: ParseLevel = .module,
    errors: ArrayList(ParseError),
    program: AstNode = AstNode.null_handle,
    source_text: []const u8 = "",
    pass: i32 = 0,
    unbound: i32 = 0,

    pub fn init(gpa: std.mem.Allocator) Parser {
        return .{
            .gpa = gpa,
            .arena = std.heap.ArenaAllocator.init(gpa),
            .nodes = ArrayList(AstNodeImpl).init(gpa),
            .namespace_nodes = ArrayList(Namespace).init(gpa),
            .namespaces = ArrayList(NsHandle).init(gpa),
            .lexer = Lexer.init(gpa),
            .errors = ArrayList(ParseError).init(gpa),
        };
    }

    pub fn deinit(self: *Parser) void {
        for (self.namespace_nodes.items) |*ns| ns.deinit();
        self.namespace_nodes.deinit();
        self.namespaces.deinit();
        self.nodes.deinit();
        self.lexer.deinit();
        self.errors.deinit();
        self.arena.deinit();
    }

    // ── Node creation ─────────────────────────────────────────────────────────

    pub fn makeNode(self: *Parser, loc: TokenLocation, node: SyntaxNode) !AstNode {
        const handle = try AstNode.append(&self.nodes, .{
            .location = loc,
            .node = node,
        });
        handle.get().id = handle;
        return handle;
    }

    // "copy_node" pattern: derive a new node from an existing one, inheriting ns.
    pub fn deriveNode(self: *Parser, from: AstNode, node: SyntaxNode) !AstNode {
        const ret = try self.makeNode(from.getConst().location, node);
        ret.get().ns = from.getConst().ns;
        ret.get().supercedes = from;
        from.get().superceded_by = ret;
        return ret;
    }

    // ── Source text helpers ───────────────────────────────────────────────────

    fn tokenText(self: *Parser, tok: Token) []const u8 {
        return self.lexer.tokenText(tok);
    }

    fn dupeText(self: *Parser, tok: Token) ![]const u8 {
        return self.arena.allocator().dupe(u8, self.lexer.tokenText(tok));
    }

    fn dupeStr(self: *Parser, s: []const u8) ![]const u8 {
        return self.arena.allocator().dupe(u8, s);
    }

    fn textAt(self: *Parser, start: usize, end: ?usize) []const u8 {
        if (start >= self.source_text.len) return "";
        const e = end orelse self.source_text.len;
        return self.source_text[start..@min(e, self.source_text.len)];
    }

    // ── Error recording ───────────────────────────────────────────────────────

    pub fn appendError(self: *Parser, loc: TokenLocation, comptime fmt: []const u8, args: anytype) void {
        const msg = std.fmt.allocPrint(self.arena.allocator(), fmt, args) catch "(allocation failed)";
        self.errors.append(.{ .location = loc, .message = msg }) catch {};
    }

    pub fn appendTokenError(self: *Parser, tok: Token, comptime fmt: []const u8, args: anytype) void {
        self.appendError(tok.location, fmt, args);
    }

    // ── Top-level parse entry points ──────────────────────────────────────────

    pub fn parseProgram(self: *Parser, name: []const u8, text: []const u8) !?AstNode {
        self.source_text = text;
        try self.lexer.pushSource(text);
        const duped_name = try self.dupeStr(name);
        const root = try self.makeNode(.{}, .{ .program = .{
            .name = duped_name,
            .source = text,
            .statements = &.{},
        } });
        var stmts = ArrayList(AstNode).init(self.arena.allocator());
        const end_tok = try self.parseStatements(&stmts, .module);
        if (end_tok.kind != .eof) {
            self.appendTokenError(end_tok, "Expected end of file", .{});
            return null;
        }
        if (stmts.items.len == 0) return null;
        const slice = try stmts.toOwnedSlice();
        root.get().location = TokenLocation.merge(slice[0].getConst().location, slice[slice.len - 1].getConst().location);
        root.get().node.program.statements = slice;
        self.program = root;
        return root;
    }

    pub fn parseModule(self: *Parser, name: []const u8, text: []const u8) !?AstNode {
        self.source_text = text;
        try self.lexer.pushSource(text);
        const duped_name = try self.dupeStr(name);
        const root = try self.makeNode(.{}, .{ .module = .{
            .name = duped_name,
            .source = text,
            .statements = &.{},
        } });
        var stmts = ArrayList(AstNode).init(self.arena.allocator());
        const end_tok = try self.parseStatements(&stmts, .module);
        if (end_tok.kind != .eof) {
            self.appendTokenError(end_tok, "Expected end of file", .{});
            return null;
        }
        if (stmts.items.len == 0) return null;
        const slice = try stmts.toOwnedSlice();
        root.get().location = TokenLocation.merge(slice[0].getConst().location, slice[slice.len - 1].getConst().location);
        root.get().node.module.statements = slice;
        return root;
    }

    // ── Statement list ────────────────────────────────────────────────────────

    fn parseStatements(self: *Parser, out: *ArrayList(AstNode), parse_level: ParseLevel) !Token {
        const saved_level = self.level;
        self.level = parse_level;
        defer self.level = saved_level;

        while (true) {
            const t = self.lexer.peek();
            if (t.kind == .eof or t.matchesSymbol('}')) {
                _ = self.lexer.lex();
                return t;
            }
            const stmt_opt = if (self.level == .module)
                try self.parseModuleLevelStatement()
            else
                try self.parseStatement();
            if (stmt_opt) |stmt| try out.append(stmt);
        }
    }

    // ── Module-level statement ────────────────────────────────────────────────

    fn parseModuleLevelStatement(self: *Parser) anyerror!?AstNode {
        const t = self.lexer.peek();
        switch (t.kind) {
            .eof => {
                self.appendTokenError(t, "Unexpected end of file", .{});
                return null;
            },
            .identifier => {
                _ = self.lexer.lex();
                _ = self.lexer.expectSymbol(':') catch {
                    self.appendError(self.lexer.last_location, "Expected variable declaration", .{});
                    return null;
                };
                return self.parseStatement();
            },
            .keyword => switch (t.value.keyword) {
                .alias   => return self.parseAlias(),
                .@"const" => { _ = self.lexer.lex(); return self.parseModuleLevelStatement(); },
                .@"enum" => return self.parseEnum(),
                .@"export" => return self.parseExportPublic(),
                .@"extern" => return self.parseExtern(),
                .func    => return self.parseFunc(),
                .import  => return self.parseImport(),
                .include => return self.parseInclude(),
                .public  => return self.parseExportPublic(),
                .@"struct" => return self.parseStruct(),
                else => {},
            },
            else => {},
        }
        _ = self.lexer.lex();
        self.appendTokenError(t, "Unexpected token in module", .{});
        return null;
    }

    // ── Block-level statement ─────────────────────────────────────────────────

    fn parseStatement(self: *Parser) anyerror!?AstNode {
        const t = self.lexer.peek();
        switch (t.kind) {
            .eof => {
                self.appendTokenError(t, "Unexpected end of file", .{});
                return null;
            },
            .identifier => {
                // Check lookback for labeled statement or var decl
                if (self.lexer.hasLookback(1) and
                    self.lexer.lookbackAt(0).matchesSymbol(':') and
                    self.lexer.lookbackAt(1).kind == .identifier)
                {
                    return self.parseVarDecl();
                }
                _ = self.lexer.lex();
                if (self.lexer.peek().matchesSymbol(':')) {
                    _ = self.lexer.lex();
                    return self.parseStatement();
                }
                // Push back and fall through to expression
                self.lexer.pushToken(t) catch {};
                return self.parseExpression(0);
            },
            .number, .string => return self.parseExpression(0),
            .keyword => switch (t.value.keyword) {
                .alias    => return self.parseAlias(),
                .@"break", .@"continue" => return self.parseBreakContinue(),
                .@"const" => { _ = self.lexer.lex(); return self.parseStatement(); },
                .@"defer" => return self.parseDefer(),
                .embed    => return self.parseEmbed(),
                .@"enum"  => return self.parseEnum(),
                .@"export" => return self.parseExportPublic(),
                .@"for"   => return self.parseFor(),
                .func     => return self.parseFunc(),
                .@"if"    => return self.parseIf(),
                .include  => return self.parseInclude(),
                .loop     => return self.parseLoop(),
                .public   => return self.parseExportPublic(),
                .@"return" => return self.parseReturn(),
                .@"struct" => return self.parseStruct(),
                .@"switch" => return self.parseSwitch(),
                .@"while" => return self.parseWhile(),
                .yield    => return self.parseYield(),
                else => {
                    self.appendTokenError(t, "Unexpected keyword in statement", .{});
                    _ = self.lexer.lex();
                    return null;
                },
            },
            .symbol => switch (t.value.symbol) {
                ';' => {
                    const loc = self.lexer.lex().location;
                    return try self.makeNode(loc, .{ .dummy = .{} });
                },
                '{' => {
                    var label: Label = null;
                    if (self.lexer.hasLookback(1) and
                        self.lexer.lookbackAt(0).matchesSymbol(':') and
                        self.lexer.lookbackAt(1).kind == .identifier)
                    {
                        label = try self.dupeText(self.lexer.lookbackAt(1));
                    }
                    const open = self.lexer.lex();
                    var block_stmts = ArrayList(AstNode).init(self.arena.allocator());
                    const end_tok = try self.parseStatements(&block_stmts, .block);
                    if (!end_tok.matchesSymbol('}')) {
                        self.appendTokenError(open, "Unexpected end of statement block", .{});
                        return null;
                    }
                    if (block_stmts.items.len == 0) {
                        return try self.makeNode(TokenLocation.merge(open.location, end_tok.location), .{ .void_node = .{} });
                    }
                    const slice = try block_stmts.toOwnedSlice();
                    return try self.makeNode(TokenLocation.merge(open.location, end_tok.location), .{ .block = .{
                        .statements = slice,
                        .label = label,
                    } });
                },
                '=', '?', '&', '[' => {
                    if (self.lexer.hasLookback(1) and
                        self.lexer.lookbackAt(0).matchesSymbol(':') and
                        self.lexer.lookbackAt(1).kind == .identifier)
                    {
                        return self.parseVarDecl();
                    }
                    return self.parseExpression(0);
                },
                else => return self.parseExpression(0),
            },
            else => {
                _ = self.lexer.lex();
                self.appendTokenError(t, "Unexpected token in statement", .{});
                return null;
            },
        }
    }

    // ── Primary expression ────────────────────────────────────────────────────

    fn parsePrimary(self: *Parser) error{OutOfMemory}!?AstNode {
        const tok = self.lexer.peek();
        switch (tok.kind) {
            .number => return self.parseNumber(tok),
            .string => {
                _ = self.lexer.lex();
                // Single-quote strings are char literals
                if (tok.value.quote == .single and tok.location.length != 3) {
                    // length=1(quote)+1(char)+1(quote)=3; tokenText includes the quotes
                    self.appendTokenError(tok, "Single-quoted string must contain exactly one character", .{});
                    return null;
                }
                const raw = self.tokenText(tok);
                // Strip surrounding quotes from the raw text
                const text = if (raw.len >= 2) raw[1 .. raw.len - 1] else raw;
                const duped = try self.dupeStr(text);
                return try self.makeNode(tok.location, .{ .quoted_string = .{
                    .string = duped,
                    .quote_type = tok.value.quote,
                } });
            },
            .identifier => {
                _ = self.lexer.lex();
                const text = try self.dupeText(tok);
                return try self.makeNode(tok.location, .{ .identifier = .{ .identifier = text } });
            },
            .keyword => {
                if (tok.matchesKeyword(.embed))   return self.parseEmbed();
                if (tok.matchesKeyword(.include))  return self.parseInclude();
                if (tok.matchesKeyword(.false)) {
                    _ = self.lexer.lex();
                    return try self.makeNode(tok.location, .{ .bool_constant = .{ .value = false } });
                }
                if (tok.matchesKeyword(.true)) {
                    _ = self.lexer.lex();
                    return try self.makeNode(tok.location, .{ .bool_constant = .{ .value = true } });
                }
                if (tok.matchesKeyword(.null)) {
                    _ = self.lexer.lex();
                    return try self.makeNode(tok.location, .{ .null_ptr = .{} });
                }
                if (self.checkPrefixOp()) |op_def| {
                    const bp = bindingPower(op_def);
                    const op_tok = self.lexer.lex();
                    const operand_opt = if (op_def.op == .sizeof)
                        try self.parseType()
                    else
                        try self.parseExpression(bp.right);
                    const operand = operand_opt orelse {
                        self.appendTokenError(tok, "Expected operand after prefix operator", .{});
                        return null;
                    };
                    return try self.makeNode(TokenLocation.merge(op_tok.location, operand.getConst().location), .{
                        .unary_expression = .{ .op = op_def.op, .operand = operand },
                    });
                }
                self.appendTokenError(tok, "Unexpected keyword in primary expression", .{});
                return null;
            },
            .symbol => {
                if (tok.matchesSymbol('(')) {
                    _ = self.lexer.lex();
                    if (self.lexer.acceptSymbol(')')) {
                        return try self.makeNode(tok.location, .{ .void_node = .{} });
                    }
                    const inner = try self.parseExpression(0) orelse {
                        self.appendTokenError(tok, "Expected expression after '('", .{});
                        return null;
                    };
                    _ = self.lexer.expectSymbol(')') catch {
                        self.appendError(self.lexer.last_location, "Expected ')'", .{});
                        return null;
                    };
                    return inner;
                }
                if (tok.matchesSymbol('{')) {
                    _ = self.lexer.lex();
                    if (self.lexer.acceptSymbol('}')) {
                        return try self.makeNode(tok.location, .{ .void_node = .{} });
                    }
                    return self.parseBracedInitializer();
                }
                if (self.checkPrefixOp()) |op_def| {
                    const bp = bindingPower(op_def);
                    const op_tok = self.lexer.lex();
                    const operand = try self.parseExpression(bp.right) orelse {
                        self.appendTokenError(tok, "Expected operand after prefix operator", .{});
                        return null;
                    };
                    return try self.makeNode(TokenLocation.merge(op_tok.location, operand.getConst().location), .{
                        .unary_expression = .{ .op = op_def.op, .operand = operand },
                    });
                }
                self.appendTokenError(tok, "Unexpected symbol in primary expression", .{});
                return null;
            },
            else => {
                self.appendTokenError(tok, "Unexpected token in primary expression", .{});
                return null;
            },
        }
    }

    // ── Number literal ────────────────────────────────────────────────────────

    fn parseNumber(self: *Parser, tok: Token) !?AstNode {
        _ = self.lexer.lex(); // consume the number token
        const whole = self.tokenText(tok);

        // Check for decimal: number '.' number?
        var frac: ?[]const u8 = null;
        var exponent: ?[]const u8 = null;
        if (self.lexer.acceptSymbol('.')) {
            if (self.lexer.acceptNumber()) |frac_tok| {
                frac = self.tokenText(frac_tok);
            }
        }

        // Check for exponent: 'e'/'E' number?
        const bm = self.lexer.bookmark();
        if (self.lexer.acceptIdentifier()) |e_tok| {
            const e_text = self.tokenText(e_tok);
            if (std.mem.eql(u8, e_text, "e") or std.mem.eql(u8, e_text, "E")) {
                if (self.lexer.acceptNumber()) |exp_tok| {
                    exponent = self.tokenText(exp_tok);
                }
            } else {
                self.lexer.rewind(bm);
            }
        }

        const end_loc = self.lexer.last_location;
        const merged_loc = TokenLocation.merge(tok.location, end_loc);

        if (frac != null or exponent != null) {
            const value = parseDecimalValue(whole, frac, exponent) catch 0.0;
            return try self.makeNode(merged_loc, .{ .decimal = .{ .value = value } });
        }

        const radix: Radix = if (tok.kind == .number) tok.value.radix else .decimal;
        const int_val = parseIntValue(whole, radix) catch {
            self.appendTokenError(tok, "Invalid integer literal", .{});
            return null;
        };
        return try self.makeNode(merged_loc, .{ .number = .{ .value = int_val } });
    }

    // ── Pratt expression parser ───────────────────────────────────────────────

    pub fn parseExpression(self: *Parser, min_prec: Precedence) error{OutOfMemory}!?AstNode {
        var lhs = try self.parsePrimary() orelse return null;

        while (!self.lexer.nextMatches(.eof) and self.checkOp()) {
            // Postfix operator?
            if (self.checkPostfixOp()) |op_def| {
                const bp = bindingPower(op_def);
                if (bp.left < min_prec) break;
                if (op_def.op == .subscript) {
                    _ = self.lexer.lex(); // consume '['
                    const rhs = try self.parseExpression(0) orelse {
                        self.appendError(self.lexer.peek().location, "Expected subscript expression", .{});
                        return null;
                    };
                    _ = self.lexer.expectSymbol(']') catch {
                        self.appendError(self.lexer.last_location, "Expected ']'", .{});
                        return null;
                    };
                    lhs = try self.makeNode(
                        TokenLocation.merge(lhs.getConst().location, rhs.getConst().location),
                        .{ .binary_expression = .{ .lhs = lhs, .op = .subscript, .rhs = rhs } });
                } else {
                    const loc = TokenLocation.merge(lhs.getConst().location, self.lexer.peek().location);
                    _ = self.lexer.lex();
                    lhs = try self.makeNode(loc, .{ .unary_expression = .{ .op = op_def.op, .operand = lhs } });
                }
                continue;
            }

            // Binary / infix operator?
            if (self.checkBinop()) |op_def| {
                const bp = bindingPower(op_def);
                if (bp.left < min_prec) break;
                if (op_def.op == .call) {
                    // Don't consume '(' — parsePrimary handles the parens + arg list
                    const param_list = try self.parsePrimary() orelse {
                        self.appendError(lhs.getConst().location, "Could not parse function call arguments", .{});
                        return null;
                    };
                    lhs = try self.makeNode(
                        TokenLocation.merge(lhs.getConst().location, param_list.getConst().location),
                        .{ .binary_expression = .{ .lhs = lhs, .op = .call, .rhs = param_list } });
                } else {
                    _ = self.lexer.lex(); // consume the operator token
                    const rhs = if (op_def.op == .cast)
                        try self.parseType()
                    else
                        try self.parseExpression(bp.right);
                    const rhs_node = rhs orelse return null;
                    lhs = try self.makeNode(
                        TokenLocation.merge(lhs.getConst().location, rhs_node.getConst().location),
                        .{ .binary_expression = .{ .lhs = lhs, .op = op_def.op, .rhs = rhs_node } });
                }
                continue;
            }
            break;
        }
        return lhs;
    }

    // ── Operator lookahead helpers ────────────────────────────────────────────

    fn checkOp(self: *Parser) bool {
        const tok = self.lexer.peek();
        if (tok.kind != .symbol and tok.kind != .keyword) return false;
        for (operator_table) |def| if (symMatches(def.sym, tok)) return true;
        return false;
    }

    fn checkBinop(self: *Parser) ?OperatorDef {
        const tok = self.lexer.peek();
        if (tok.kind != .symbol and tok.kind != .keyword) return null;
        for (operator_table) |def| {
            if (def.position == .infix and symMatches(def.sym, tok)) return def;
        }
        return null;
    }

    fn checkPrefixOp(self: *Parser) ?OperatorDef {
        const tok = self.lexer.peek();
        if (tok.kind != .symbol and tok.kind != .keyword) return null;
        for (operator_table) |def| {
            if (def.position == .prefix and symMatches(def.sym, tok)) return def;
        }
        return null;
    }

    fn checkPostfixOp(self: *Parser) ?OperatorDef {
        const tok = self.lexer.peek();
        if (tok.kind != .symbol and tok.kind != .keyword) return null;
        for (operator_table) |def| {
            if (def.position == .postfix and symMatches(def.sym, tok)) return def;
        }
        return null;
    }

    // ── Braced initializer ────────────────────────────────────────────────────

    fn parseBracedInitializer(self: *Parser) !?AstNode {
        const expr = try self.parseExpression(0) orelse return null;
        _ = self.lexer.expectSymbol('}') catch {
            self.appendError(self.lexer.last_location, "Expected '}}'", .{});
            return null;
        };
        return expr;
    }

    // ── Type specification ────────────────────────────────────────────────────

    pub fn parseType(self: *Parser) !?AstNode {
        const t = self.lexer.peek();

        if (self.lexer.acceptSymbol('&')) {
            const inner = try self.parseType() orelse return null;
            return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                .type_specification = .{ .description = .{ .reference = .{ .referencing = inner } } },
            });
        }
        if (self.lexer.acceptSymbol('?')) {
            const inner = try self.parseType() orelse return null;
            return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                .type_specification = .{ .description = .{ .optional = .{ .optional_of = inner } } },
            });
        }
        if (self.lexer.acceptSymbol('[')) {
            if (self.lexer.acceptSymbol(']')) {
                const inner = try self.parseType() orelse return null;
                return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                    .type_specification = .{ .description = .{ .slice = .{ .slice_of = inner } } },
                });
            }
            if (self.lexer.acceptSymbol('0')) {
                _ = self.lexer.expectSymbol(']') catch {
                    self.appendError(self.lexer.last_location, "Expected ']' to close '[0'", .{});
                    return null;
                };
                const inner = try self.parseType() orelse return null;
                return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                    .type_specification = .{ .description = .{ .zero_terminated_array = .{ .array_of = inner } } },
                });
            }
            if (self.lexer.acceptSymbol('*')) {
                _ = self.lexer.expectSymbol(']') catch {
                    self.appendError(self.lexer.last_location, "Expected ']' to close '[*'", .{});
                    return null;
                };
                const inner = try self.parseType() orelse return null;
                return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                    .type_specification = .{ .description = .{ .dyn_array = .{ .array_of = inner } } },
                });
            }
            // Fixed-size array: [N]T
            const size_tok = self.lexer.expectNumber() catch {
                self.appendError(self.lexer.last_location, "Expected integer array size, '0', ']', or '*'", .{});
                return null;
            };
            _ = self.lexer.expectSymbol(']') catch {
                self.appendError(self.lexer.last_location, "Expected ']' after array size", .{});
                return null;
            };
            const size_text = self.tokenText(size_tok);
            const size = std.fmt.parseUnsigned(usize, size_text, 10) catch {
                self.appendTokenError(size_tok, "Invalid array size", .{});
                return null;
            };
            const inner = try self.parseType() orelse return null;
            return try self.makeNode(TokenLocation.merge(t.location, inner.getConst().location), .{
                .type_specification = .{ .description = .{ .array = .{ .array_of = inner, .size = size } } },
            });
        }

        // Named type, possibly dotted (module.Type) and/or generic (Type<A,B>)
        var name_parts = ArrayList([]const u8).init(self.arena.allocator());
        const start = self.lexer.peek().location;
        while (true) {
            const n_tok = self.lexer.acceptIdentifier() orelse {
                self.appendError(self.lexer.peek().location, "Expected type name", .{});
                return null;
            };
            try name_parts.append(try self.dupeText(n_tok));
            if (!self.lexer.acceptSymbol('.')) break;
        }
        if (name_parts.items.len == 0) return null;

        var args = ArrayList(AstNode).init(self.arena.allocator());
        if (self.lexer.acceptSymbol('<')) {
            while (true) {
                if (self.lexer.acceptSymbol('>')) break;
                const arg = try self.parseType() orelse {
                    self.appendError(self.lexer.peek().location, "Expected type argument", .{});
                    return null;
                };
                try args.append(arg);
                if (self.lexer.acceptSymbol('>')) break;
                _ = self.lexer.expectSymbol(',') catch {
                    self.appendError(self.lexer.last_location, "Expected ',' or '>'", .{});
                    return null;
                };
            }
        }

        const name_slice = try name_parts.toOwnedSlice();
        const args_slice = try args.toOwnedSlice();
        const type_node = try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
            .type_specification = .{ .description = .{ .type_name = .{ .name = name_slice, .arguments = args_slice } } },
        });

        // Optional result type: T/E
        if (self.lexer.acceptSymbol('/')) {
            const err_type = try self.parseType() orelse return null;
            return try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
                .type_specification = .{ .description = .{ .result = .{
                    .success = type_node,
                    .@"error" = err_type,
                } } },
            });
        }
        return type_node;
    }

    // ── Alias ─────────────────────────────────────────────────────────────────

    fn parseAlias(self: *Parser) !?AstNode {
        const kw = self.lexer.lex(); // consume 'alias'
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected alias name", .{});
            return null;
        };
        const aliased = try self.parseType() orelse {
            self.appendError(self.lexer.last_location, "Expected aliased type", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(kw.location, self.lexer.last_location), .{ .alias = .{
            .name = try self.dupeText(name_tok),
            .aliased_type = aliased,
        } });
    }

    // ── Break / Continue ──────────────────────────────────────────────────────

    fn parseBreakContinue(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        var label: Label = null;
        if (self.lexer.acceptSymbol(':')) {
            const lbl_tok = self.lexer.peek();
            if (lbl_tok.kind != .identifier) {
                self.appendTokenError(lbl_tok, "Expected label name after ':'", .{});
                return null;
            }
            _ = self.lexer.lex();
            label = try self.dupeText(lbl_tok);
        }
        if (kw.matchesKeyword(.@"break")) {
            return try self.makeNode(kw.location, .{ .@"break" = .{ .label = label, .block = AstNode.null_handle } });
        }
        return try self.makeNode(kw.location, .{ .@"continue" = .{ .label = label } });
    }

    // ── Defer ─────────────────────────────────────────────────────────────────

    fn parseDefer(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        const stmt = try self.parseStatement() orelse {
            self.appendTokenError(kw, "Could not parse defer statement", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(kw.location, stmt.getConst().location), .{
            .defer_statement = .{ .statement = stmt },
        });
    }

    // ── Embed ─────────────────────────────────────────────────────────────────

    fn parseEmbed(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        _ = self.lexer.expectSymbol('(') catch {
            self.appendError(self.lexer.last_location, "Expected '(' after @embed", .{});
            return null;
        };
        const fn_tok = self.lexer.expect(.string) catch {
            self.appendError(self.lexer.last_location, "Expected file name string in @embed", .{});
            return null;
        };
        _ = self.lexer.expectSymbol(')') catch {
            self.appendError(self.lexer.last_location, "Expected ')' after @embed file name", .{});
            return null;
        };
        const raw = self.tokenText(fn_tok);
        const fname = if (raw.len >= 2) raw[1 .. raw.len - 1] else raw;
        return try self.makeNode(TokenLocation.merge(kw.location, self.lexer.last_location), .{
            .embed = .{ .file_name = try self.dupeStr(fname) },
        });
    }

    // ── Enum ──────────────────────────────────────────────────────────────────

    fn parseEnum(self: *Parser) !?AstNode {
        const enum_tok = self.lexer.lex();
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected enum name", .{});
            return null;
        };
        var underlying = AstNode.null_handle;
        if (self.lexer.acceptSymbol(':')) {
            underlying = try self.parseType() orelse {
                self.appendError(self.lexer.last_location, "Expected underlying type", .{});
                return null;
            };
        }
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' to open enum body", .{});
            return null;
        };
        var values = ArrayList(AstNode).init(self.arena.allocator());
        while (!self.lexer.acceptSymbol('}')) {
            const lbl_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected enum value label", .{});
                return null;
            };
            var payload = AstNode.null_handle;
            if (self.lexer.acceptSymbol(':')) {
                payload = try self.parseType() orelse {
                    self.appendError(self.lexer.last_location, "Expected payload type", .{});
                    return null;
                };
            }
            var value_node = AstNode.null_handle;
            if (self.lexer.acceptSymbol('=')) {
                const num_tok = self.lexer.peek();
                if (num_tok.kind != .number) {
                    self.appendTokenError(num_tok, "Expected numeric enum value", .{});
                    return null;
                }
                _ = self.lexer.lex();
                value_node = try self.makeNumber(num_tok) orelse return null;
            }
            const ev = try self.makeNode(TokenLocation.merge(lbl_tok.location, self.lexer.last_location), .{
                .enum_value = .{
                    .label = try self.dupeText(lbl_tok),
                    .value = value_node,
                    .payload = payload,
                },
            });
            try values.append(ev);
            if (!self.lexer.acceptSymbol(',') and !self.lexer.nextMatches(.symbol)) {
                // next should be '}' for the while condition to catch
            }
        }
        return try self.makeNode(TokenLocation.merge(enum_tok.location, self.lexer.last_location), .{
            .@"enum" = .{
                .name = try self.dupeText(name_tok),
                .underlying_type = underlying,
                .values = try values.toOwnedSlice(),
            },
        });
    }

    // ── Function declaration (shared by func and extern) ──────────────────────

    fn parseFuncDecl(self: *Parser, func_tok: Token) !?AstNode {
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected function name", .{});
            return null;
        };

        var generics = ArrayList(AstNode).init(self.arena.allocator());
        if (self.lexer.acceptSymbol('<')) {
            while (true) {
                if (self.lexer.acceptSymbol('>')) break;
                const gn_tok = self.lexer.expectIdentifier() catch {
                    self.appendError(self.lexer.last_location, "Expected generic type parameter name", .{});
                    return null;
                };
                try generics.append(try self.makeNode(gn_tok.location, .{
                    .identifier = .{ .identifier = try self.dupeText(gn_tok) },
                }));
                if (self.lexer.acceptSymbol('>')) break;
                _ = self.lexer.expectSymbol(',') catch {};
            }
        }

        _ = self.lexer.expectSymbol('(') catch {
            self.appendError(self.lexer.last_location, "Expected '(' in function signature", .{});
            return null;
        };
        var params = ArrayList(AstNode).init(self.arena.allocator());
        while (true) {
            if (self.lexer.acceptSymbol(')')) break;
            const pname_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected parameter name", .{});
                return null;
            };
            _ = self.lexer.expectSymbol(':') catch {
                self.appendError(self.lexer.last_location, "Expected ':' after parameter name", .{});
                return null;
            };
            const ptype = try self.parseType() orelse {
                self.appendError(self.lexer.last_location, "Expected parameter type", .{});
                return null;
            };
            try params.append(try self.makeNode(TokenLocation.merge(pname_tok.location, ptype.getConst().location), .{
                .parameter = .{ .name = try self.dupeText(pname_tok), .type_name = ptype },
            }));
            if (self.lexer.acceptSymbol(')')) break;
            _ = self.lexer.expectSymbol(',') catch {};
        }

        const ret_type = try self.parseType() orelse {
            self.appendError(self.lexer.last_location, "Expected return type", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(func_tok.location, ret_type.getConst().location), .{
            .function_declaration = .{
                .name = try self.dupeText(name_tok),
                .generics = try generics.toOwnedSlice(),
                .parameters = try params.toOwnedSlice(),
                .return_type = ret_type,
            },
        });
    }

    // ── Func definition ───────────────────────────────────────────────────────

    fn parseFunc(self: *Parser) !?AstNode {
        const saved_level = self.level;
        self.level = .function;
        defer self.level = saved_level;

        const func_tok = self.lexer.lex();
        const decl = try self.parseFuncDecl(func_tok) orelse return null;

        // extern link: func name(...) ret -> "c_name"
        if (self.lexer.acceptKeyword(.extern_link)) {
            const link_tok = self.lexer.expect(.string) catch {
                self.appendError(self.lexer.last_location, "Expected extern link name string", .{});
                return null;
            };
            const raw = self.tokenText(link_tok);
            const link_name = if (raw.len >= 2) raw[1 .. raw.len - 1] else raw;
            const ext_link = try self.makeNode(link_tok.location, .{
                .extern_link = .{ .link_name = try self.dupeStr(link_name) },
            });
            const fn_name = decl.getConst().node.function_declaration.name;
            return try self.makeNode(TokenLocation.merge(decl.getConst().location, link_tok.location), .{
                .function_definition = .{ .name = fn_name, .declaration = decl, .implementation = ext_link },
            });
        }

        const impl = try self.parseStatement() orelse {
            self.appendTokenError(func_tok, "Could not parse function body", .{});
            return null;
        };
        const fn_name = decl.getConst().node.function_declaration.name;
        return try self.makeNode(TokenLocation.merge(decl.getConst().location, impl.getConst().location), .{
            .function_definition = .{ .name = fn_name, .declaration = decl, .implementation = impl },
        });
    }

    // ── If statement ──────────────────────────────────────────────────────────

    fn parseIf(self: *Parser) !?AstNode {
        var label: Label = null;
        var loc = self.lexer.last_location;
        if (self.lexer.hasLookback(1) and
            self.lexer.lookbackAt(0).matchesSymbol(':') and
            self.lexer.lookbackAt(1).kind == .identifier)
        {
            label = try self.dupeText(self.lexer.lookbackAt(1));
            loc = self.lexer.lookbackAt(1).location;
        }
        const if_tok = self.lexer.lex();
        const condition = try self.parseExpression(0) orelse {
            self.appendTokenError(if_tok, "Error parsing 'if' condition", .{});
            return null;
        };
        const if_branch = try self.parseStatement() orelse {
            self.appendTokenError(if_tok, "Error parsing 'if' branch", .{});
            return null;
        };
        var else_branch = AstNode.null_handle;
        if (self.lexer.acceptKeyword(.@"else")) {
            else_branch = try self.parseStatement() orelse {
                self.appendTokenError(if_tok, "Error parsing 'else' branch", .{});
                return null;
            };
        }
        const end_loc = if (!else_branch.isNull()) else_branch.getConst().location else if_branch.getConst().location;
        return try self.makeNode(TokenLocation.merge(loc, end_loc), .{ .if_statement = .{
            .condition = condition,
            .if_branch = if_branch,
            .else_branch = else_branch,
            .label = label,
        } });
    }

    // ── Import ────────────────────────────────────────────────────────────────

    fn parseImport(self: *Parser) !?AstNode {
        const imp_tok = self.lexer.lex();
        var path = ArrayList([]const u8).init(self.arena.allocator());
        var end_loc = imp_tok.location;
        while (true) {
            const seg = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected import path component", .{});
                return null;
            };
            try path.append(try self.dupeText(seg));
            end_loc = seg.location;
            if (!self.lexer.acceptSymbol('.')) break;
        }
        return try self.makeNode(TokenLocation.merge(imp_tok.location, end_loc), .{
            .import = .{ .file_name = try path.toOwnedSlice() },
        });
    }

    // ── Include ───────────────────────────────────────────────────────────────

    fn parseInclude(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        _ = self.lexer.expectSymbol('(') catch {
            self.appendError(self.lexer.last_location, "Expected '(' after @include", .{});
            return null;
        };
        const fn_tok = self.lexer.expect(.string) catch {
            self.appendError(self.lexer.last_location, "Expected file name in @include", .{});
            return null;
        };
        const close = self.lexer.peek();
        _ = self.lexer.expectSymbol(')') catch {
            self.appendError(self.lexer.last_location, "Expected ')' in @include", .{});
            return null;
        };
        const raw = self.tokenText(fn_tok);
        const fname = if (raw.len >= 2) raw[1 .. raw.len - 1] else raw;
        return try self.makeNode(TokenLocation.merge(kw.location, close.location), .{
            .include = .{ .file_name = try self.dupeStr(fname) },
        });
    }

    // ── Loop ──────────────────────────────────────────────────────────────────

    fn parseLoop(self: *Parser) !?AstNode {
        var label: Label = null;
        var loc = self.lexer.peek().location;
        if (self.lexer.hasLookback(1) and
            self.lexer.lookbackAt(0).matchesSymbol(':') and
            self.lexer.lookbackAt(1).kind == .identifier)
        {
            label = try self.dupeText(self.lexer.lookbackAt(1));
            loc = self.lexer.lookbackAt(1).location;
        }
        _ = self.lexer.lex(); // consume 'loop'
        const stmt = try self.parseStatement() orelse {
            self.appendError(loc, "Error parsing 'loop' body", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(loc, stmt.getConst().location), .{
            .loop_statement = .{ .label = label, .statement = stmt },
        });
    }

    // ── Export / Public ───────────────────────────────────────────────────────

    fn parseExportPublic(self: *Parser) !?AstNode {
        const t = self.lexer.peek();
        const is_export = t.matchesKeyword(.@"export");
        _ = self.lexer.lex();
        const decl = try self.parseModuleLevelStatement() orelse return null;
        const decl_name: ?[]const u8 = switch (decl.getConst().node) {
            .function_definition => |n| n.name,
            .variable_declaration => |n| n.name,
            else => null,
        };
        const name = decl_name orelse {
            self.appendError(decl.getConst().location, "Cannot export/public this declaration type", .{});
            return null;
        };
        const loc = TokenLocation.merge(t.location, decl.getConst().location);
        if (is_export) {
            return try self.makeNode(loc, .{ .export_declaration = .{ .name = name, .declaration = decl } });
        }
        return try self.makeNode(loc, .{ .public_declaration = .{ .name = name, .declaration = decl } });
    }

    // ── Return ────────────────────────────────────────────────────────────────

    fn parseReturn(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        const expr = try self.parseExpression(0) orelse {
            self.appendTokenError(kw, "Error parsing return expression", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(kw.location, expr.getConst().location), .{
            .@"return" = .{ .expression = expr },
        });
    }

    // ── Struct ────────────────────────────────────────────────────────────────

    fn parseStruct(self: *Parser) !?AstNode {
        const struct_tok = self.lexer.lex();
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected struct name", .{});
            return null;
        };
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' in struct definition", .{});
            return null;
        };
        var members = ArrayList(AstNode).init(self.arena.allocator());
        while (!self.lexer.acceptSymbol('}')) {
            const lbl_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected member name", .{});
                return null;
            };
            _ = self.lexer.expectSymbol(':') catch {
                self.appendError(self.lexer.last_location, "Expected ':' in struct member", .{});
                return null;
            };
            const mtype = try self.parseType() orelse {
                self.appendError(self.lexer.last_location, "Expected struct member type", .{});
                return null;
            };
            try members.append(try self.makeNode(TokenLocation.merge(lbl_tok.location, self.lexer.last_location), .{
                .struct_member = .{ .label = try self.dupeText(lbl_tok), .member_type = mtype },
            }));
            _ = self.lexer.acceptSymbol(',');
        }
        return try self.makeNode(TokenLocation.merge(struct_tok.location, self.lexer.last_location), .{
            .@"struct" = .{ .name = try self.dupeText(name_tok), .members = try members.toOwnedSlice() },
        });
    }

    // ── Variable declaration ──────────────────────────────────────────────────

    fn parseVarDecl(self: *Parser) !?AstNode {
        // Lookback: [1]=identifier(name), [0]=':'
        const is_const = self.lexer.hasLookback(2) and self.lexer.lookbackAt(2).matchesKeyword(.@"const");
        const name_tok = self.lexer.lookbackAt(1);
        const location = if (is_const) self.lexer.lookbackAt(2).location else name_tok.location;

        var type_name = AstNode.null_handle;
        if (!self.lexer.nextMatches(.symbol) or self.lexer.peek().value.symbol != '=') {
            type_name = try self.parseType() orelse {
                self.appendError(self.lexer.peek().location, "Expected variable type", .{});
                return null;
            };
        }

        var initializer = AstNode.null_handle;
        if (self.lexer.acceptSymbol('=')) {
            initializer = try self.parseExpression(0) orelse {
                self.appendError(self.lexer.last_location, "Error parsing initializer", .{});
                return null;
            };
        } else if (type_name.isNull()) {
            self.appendError(self.lexer.peek().location, "Expected '=' in variable declaration", .{});
            return null;
        }

        const end_loc = if (!initializer.isNull()) initializer.getConst().location
                        else if (!type_name.isNull()) type_name.getConst().location
                        else self.lexer.last_location;
        return try self.makeNode(TokenLocation.merge(location, end_loc), .{ .variable_declaration = .{
            .name = try self.dupeText(name_tok),
            .type_name = type_name,
            .initializer = initializer,
            .is_const = is_const,
        } });
    }

    // ── For statement ─────────────────────────────────────────────────────────

    fn parseFor(self: *Parser) !?AstNode {
        var label: Label = null;
        var loc = self.lexer.peek().location;
        if (self.lexer.hasLookback(1) and
            self.lexer.lookbackAt(0).matchesSymbol(':') and
            self.lexer.lookbackAt(1).kind == .identifier)
        {
            label = try self.dupeText(self.lexer.lookbackAt(1));
            loc = self.lexer.lookbackAt(1).location;
        }
        _ = self.lexer.lex(); // consume 'for'
        const var_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected 'for' range variable name", .{});
            return null;
        };
        // Optional 'in' keyword (identifier, not a real keyword)
        const maybe_in = self.lexer.peek();
        if (maybe_in.kind == .identifier) {
            const in_text = self.tokenText(maybe_in);
            if (std.mem.eql(u8, in_text, "in")) _ = self.lexer.lex();
        }

        // Range: expression or type
        const bm = self.lexer.bookmark();
        var range = try self.parseExpression(0);
        if (range == null) {
            self.lexer.rewind(bm);
            range = try self.parseType();
            if (range == null) {
                self.appendError(self.lexer.peek().location, "Error parsing 'for' range", .{});
                return null;
            }
        }
        const stmt = try self.parseStatement() orelse {
            self.appendError(self.lexer.peek().location, "Error parsing 'for' body", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(loc, stmt.getConst().location), .{ .for_statement = .{
            .range_variable = try self.dupeText(var_tok),
            .range_expr = range.?,
            .statement = stmt,
            .label = label,
        } });
    }

    // ── While statement ───────────────────────────────────────────────────────

    fn parseWhile(self: *Parser) !?AstNode {
        var label: Label = null;
        var loc = self.lexer.last_location;
        if (self.lexer.hasLookback(1) and
            self.lexer.lookbackAt(0).matchesSymbol(':') and
            self.lexer.lookbackAt(1).kind == .identifier)
        {
            label = try self.dupeText(self.lexer.lookbackAt(1));
            loc = self.lexer.lookbackAt(1).location;
        }
        const while_tok = self.lexer.lex();
        if (!while_tok.matchesKeyword(.@"while")) loc = while_tok.location;
        const condition = try self.parseExpression(0) orelse {
            self.appendTokenError(while_tok, "Error parsing 'while' condition", .{});
            return null;
        };
        const stmt = try self.parseStatement() orelse {
            self.appendTokenError(while_tok, "Error parsing 'while' body", .{});
            return null;
        };
        return try self.makeNode(TokenLocation.merge(loc, stmt.getConst().location), .{ .while_statement = .{
            .label = label,
            .condition = condition,
            .statement = stmt,
        } });
    }

    // ── Switch statement ──────────────────────────────────────────────────────

    fn parseSwitch(self: *Parser) !?AstNode {
        var label: Label = null;
        var loc = self.lexer.peek().location;
        if (self.lexer.hasLookback(1) and
            self.lexer.lookbackAt(0).matchesSymbol(':') and
            self.lexer.lookbackAt(1).kind == .identifier)
        {
            label = try self.dupeText(self.lexer.lookbackAt(1));
            loc = self.lexer.lookbackAt(1).location;
        }
        _ = self.lexer.lex(); // consume 'switch'
        const switch_val = try self.parseExpression(0) orelse return null;
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' after switch value", .{});
            return null;
        };
        var cases = ArrayList(AstNode).init(self.arena.allocator());
        if (!self.lexer.acceptSymbol('}')) {
            while (true) {
                const case_val = try self.parseExpression(0) orelse return null;
                _ = self.lexer.expectKeyword(.switch_case) catch {
                    self.appendError(self.lexer.last_location, "Expected '=>' in switch case", .{});
                    return null;
                };
                var binding = AstNode.null_handle;
                if (self.lexer.acceptSymbol('|')) {
                    const bn_tok = self.lexer.expectIdentifier() catch {
                        self.appendError(self.lexer.last_location, "Expected binding name", .{});
                        return null;
                    };
                    binding = try self.makeNode(bn_tok.location, .{
                        .identifier = .{ .identifier = try self.dupeText(bn_tok) },
                    });
                    _ = self.lexer.expectSymbol('|') catch {
                        self.appendError(self.lexer.last_location, "Expected closing '|'", .{});
                        return null;
                    };
                }
                const case_stmt = try self.parseStatement() orelse return null;
                _ = self.lexer.expectSymbol(';') catch {
                    self.appendError(self.lexer.last_location, "Expected ';' after switch case", .{});
                    return null;
                };
                try cases.append(try self.makeNode(
                    TokenLocation.merge(case_val.getConst().location, self.lexer.last_location),
                    .{ .switch_case = .{ .case_value = case_val, .binding = binding, .statement = case_stmt } }));
                if (self.lexer.acceptSymbol('}')) break;
            }
        }
        return try self.makeNode(TokenLocation.merge(loc, self.lexer.last_location), .{ .switch_statement = .{
            .label = label,
            .switch_value = switch_val,
            .switch_cases = try cases.toOwnedSlice(),
        } });
    }

    // ── Yield ─────────────────────────────────────────────────────────────────

    fn parseYield(self: *Parser) !?AstNode {
        const kw = self.lexer.lex();
        var label: Label = null;
        if (self.lexer.acceptSymbol(':')) {
            const lbl_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected label name after ':'", .{});
                return null;
            };
            label = try self.dupeText(lbl_tok);
        }
        const stmt = try self.parseStatement() orelse {
            self.appendTokenError(kw, "Could not parse yield expression", .{});
            return null;
        };
        return try self.makeNode(kw.location, .{ .yield = .{ .label = label, .statement = stmt } });
    }

    // ── Extern block ──────────────────────────────────────────────────────────

    fn parseExtern(self: *Parser) !?AstNode {
        const ext_tok = self.lexer.lex();
        var library: []const u8 = "";
        if (self.lexer.peek().kind == .string) {
            const lib_tok = self.lexer.lex();
            const raw = self.tokenText(lib_tok);
            library = if (raw.len >= 2) try self.dupeStr(raw[1 .. raw.len - 1]) else "";
        }
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' to open 'extern' block", .{});
            return null;
        };
        var decls = ArrayList(AstNode).init(self.arena.allocator());
        while (!self.lexer.acceptSymbol('}')) {
            const bm = self.lexer.bookmark();
            const tok = self.lexer.peek();
            var decl: ?AstNode = null;
            if (self.lexer.acceptKeyword(.func)) {
                decl = try self.parseFuncDecl(tok);
            } else if (self.lexer.peek().kind == .identifier) {
                const id_tok = self.lexer.lex();
                const id_text = self.tokenText(id_tok);
                if (std.mem.eql(u8, id_text, "typedef")) {
                    decl = try self.parseCTypedef();
                } else {
                    self.lexer.rewind(bm);
                    decl = try self.parseCFuncDecl();
                }
            } else {
                self.lexer.rewind(bm);
                decl = try self.parseCFuncDecl();
            }
            if (decl == null) {
                self.appendError(self.lexer.last_location, "Expected function or type declaration in 'extern' block", .{});
                return null;
            }
            try decls.append(decl.?);
        }
        return try self.makeNode(TokenLocation.merge(ext_tok.location, self.lexer.last_location), .{
            .@"extern" = .{ .declarations = try decls.toOwnedSlice(), .library = library },
        });
    }

    // C-style type, function, struct, enum, typedef parsing
    fn parseCType(self: *Parser) !?AstNode {
        var name: []const u8 = "";
        var is_unsigned = false;
        const start = self.lexer.peek().location;
        while (name.len == 0) {
            if (self.lexer.acceptKeyword(.@"const")) continue;
            const id_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected type name in C declaration", .{});
                return null;
            };
            const id = self.tokenText(id_tok);
            if (std.mem.eql(u8, id, "unsigned")) { is_unsigned = true; continue; }
            name = id;
        }
        // Map C type names to cathode names
        const mapped: []const u8 = blk: {
            if (std.mem.eql(u8, name, "int"))          break :blk if (is_unsigned) "u32" else "i32";
            if (std.mem.eql(u8, name, "long"))         break :blk if (is_unsigned) "u64" else "i64";
            if (std.mem.eql(u8, name, "short"))        break :blk if (is_unsigned) "u16" else "i16";
            if (std.mem.eql(u8, name, "byte"))         break :blk if (is_unsigned) "u8"  else "i8";
            if (std.mem.eql(u8, name, "uint8_t") or std.mem.eql(u8, name, "char")) break :blk "u8";
            if (std.mem.eql(u8, name, "int8_t"))       break :blk "i8";
            if (std.mem.eql(u8, name, "uint16_t"))     break :blk "u16";
            if (std.mem.eql(u8, name, "int16_t"))      break :blk "i16";
            if (std.mem.eql(u8, name, "uint32_t"))     break :blk "u32";
            if (std.mem.eql(u8, name, "int32_t"))      break :blk "i32";
            if (std.mem.eql(u8, name, "uint64_t") or std.mem.eql(u8, name, "size_t") or std.mem.eql(u8, name, "intptr_t")) break :blk "u64";
            if (std.mem.eql(u8, name, "int64_t") or std.mem.eql(u8, name, "ptrdiff_t")) break :blk "i64";
            if (std.mem.eql(u8, name, "float"))        break :blk "f32";
            if (std.mem.eql(u8, name, "double"))       break :blk "f64";
            break :blk name;
        };
        var type_node = try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
            .type_specification = .{ .description = .{ .type_name = .{
                .name = try self.arena.allocator().dupe([]const u8, &.{try self.dupeStr(mapped)}),
                .arguments = try self.arena.allocator().dupe(AstNode, &.{}),
            } } },
        });
        var is_pointer = false;
        while (self.lexer.acceptSymbol('*')) {
            if (std.mem.eql(u8, mapped, "u8") and !is_pointer) {
                type_node = try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
                    .type_specification = .{ .description = .{ .type_name = .{
                        .name = try self.arena.allocator().dupe([]const u8, &.{"cstring"}),
                        .arguments = try self.arena.allocator().dupe(AstNode, &.{}),
                    } } },
                });
            } else {
                type_node = try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
                    .type_specification = .{ .description = .{ .pointer = .{ .referencing = type_node } } },
                });
            }
            is_pointer = true;
        }
        return type_node;
    }

    fn parseCFuncDecl(self: *Parser) !?AstNode {
        const ret_type = try self.parseCType() orelse {
            self.appendError(self.lexer.last_location, "Expected return type in C function declaration", .{});
            return null;
        };
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected function name in C declaration", .{});
            return null;
        };
        _ = self.lexer.expectSymbol('(') catch {};
        var params = ArrayList(AstNode).init(self.arena.allocator());
        while (true) {
            if (self.lexer.acceptSymbol(')')) break;
            const ptype = try self.parseCType() orelse {
                self.appendError(self.lexer.last_location, "Expected parameter type in C declaration", .{});
                return null;
            };
            // Accept optional parameter name
            const pname: []const u8 = if (self.lexer.acceptIdentifier()) |n_tok|
                try self.dupeText(n_tok)
            else
                try std.fmt.allocPrint(self.arena.allocator(), "param{d}", .{params.items.len});
            try params.append(try self.makeNode(
                TokenLocation.merge(ptype.getConst().location, self.lexer.last_location),
                .{ .parameter = .{ .name = pname, .type_name = ptype } }));
            if (self.lexer.acceptSymbol(')')) break;
            _ = self.lexer.expectSymbol(',') catch {};
        }
        _ = self.lexer.acceptSymbol(';');
        return try self.makeNode(TokenLocation.merge(ret_type.getConst().location, self.lexer.last_location), .{
            .function_declaration = .{
                .name = try self.dupeText(name_tok),
                .generics = &.{},
                .parameters = try params.toOwnedSlice(),
                .return_type = ret_type,
            },
        });
    }

    fn parseCTypedef(self: *Parser) !?AstNode {
        if (self.lexer.acceptKeyword(.@"struct")) return self.parseCStruct();
        if (self.lexer.acceptKeyword(.@"enum"))   return self.parseCEnum();
        const aliased = try self.parseCType() orelse {
            self.appendError(self.lexer.last_location, "Expected type in C typedef", .{});
            return null;
        };
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected typedef name", .{});
            return null;
        };
        _ = self.lexer.acceptSymbol(';');
        return try self.makeNode(TokenLocation.merge(aliased.getConst().location, self.lexer.last_location), .{
            .alias = .{ .name = try self.dupeText(name_tok), .aliased_type = aliased },
        });
    }

    fn parseCStruct(self: *Parser) !?AstNode {
        const start = self.lexer.last_location;
        _ = self.lexer.acceptIdentifier(); // optional struct tag
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' in C struct", .{});
            return null;
        };
        var fields = ArrayList(AstNode).init(self.arena.allocator());
        while (!self.lexer.acceptSymbol('}')) {
            const ftype = try self.parseCType() orelse return null;
            const fname_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected field name in C struct", .{});
                return null;
            };
            _ = self.lexer.acceptSymbol(';');
            try fields.append(try self.makeNode(
                TokenLocation.merge(ftype.getConst().location, self.lexer.last_location),
                .{ .struct_member = .{ .label = try self.dupeText(fname_tok), .member_type = ftype } }));
        }
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected type name after C struct body", .{});
            return null;
        };
        _ = self.lexer.acceptSymbol(';');
        return try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
            .@"struct" = .{ .name = try self.dupeText(name_tok), .members = try fields.toOwnedSlice() },
        });
    }

    fn parseCEnum(self: *Parser) !?AstNode {
        const start = self.lexer.last_location;
        _ = self.lexer.acceptIdentifier(); // optional enum tag
        _ = self.lexer.expectSymbol('{') catch {
            self.appendError(self.lexer.last_location, "Expected '{{' in C enum", .{});
            return null;
        };
        var values = ArrayList(AstNode).init(self.arena.allocator());
        while (!self.lexer.acceptSymbol('}')) {
            const lbl_tok = self.lexer.expectIdentifier() catch {
                self.appendError(self.lexer.last_location, "Expected enum value label in C enum", .{});
                return null;
            };
            var val_node = AstNode.null_handle;
            if (self.lexer.acceptSymbol('=')) {
                const num_tok = self.lexer.expectNumber() catch {
                    self.appendError(self.lexer.last_location, "Expected numeric value in C enum", .{});
                    return null;
                };
                val_node = try self.makeNumber(num_tok) orelse return null;
            }
            try values.append(try self.makeNode(
                TokenLocation.merge(lbl_tok.location, self.lexer.last_location),
                .{ .enum_value = .{ .label = try self.dupeText(lbl_tok), .value = val_node, .payload = AstNode.null_handle } }));
            if (self.lexer.acceptSymbol('}')) break;
            _ = self.lexer.expectSymbol(',') catch {};
        }
        const name_tok = self.lexer.expectIdentifier() catch {
            self.appendError(self.lexer.last_location, "Expected type name after C enum body", .{});
            return null;
        };
        _ = self.lexer.acceptSymbol(';');
        return try self.makeNode(TokenLocation.merge(start, self.lexer.last_location), .{
            .@"enum" = .{ .name = try self.dupeText(name_tok), .underlying_type = AstNode.null_handle, .values = try values.toOwnedSlice() },
        });
    }

    // ── Namespace management ──────────────────────────────────────────────────

    // Create a fresh Namespace for `node` and record it.  If the node already
    // has a namespace, this is a no-op (idempotent like the C++ version).
    pub fn initNamespace(self: *Parser, node: AstNode) !void {
        if (!node.getConst().ns.isNull()) return;
        const ns = Namespace.init(self.arena.allocator(), node);
        const handle = try NsHandle.append(&self.namespace_nodes, ns);
        handle.get().id = handle;
        node.get().ns = handle;
    }

    pub fn pushNamespace(self: *Parser, node: AstNode) !void {
        const ns_handle = node.getConst().ns;
        std.debug.assert(!ns_handle.isNull());
        if (self.namespaces.items.len > 0 and ns_handle.getConst().parent.isNull()) {
            ns_handle.get().parent = self.namespaces.items[self.namespaces.items.len - 1];
        }
        try self.namespaces.append(ns_handle);
    }

    pub fn popNamespace(self: *Parser, node: AstNode) void {
        if (self.namespaces.items.len == 0) return;
        const top = self.namespaces.items[self.namespaces.items.len - 1];
        if (NsHandle.eql(top, node.getConst().ns)) {
            _ = self.namespaces.pop();
        }
    }

    pub fn currentNs(self: *const Parser) ?NsHandle {
        if (self.namespaces.items.len == 0) return null;
        return self.namespaces.items[self.namespaces.items.len - 1];
    }

    pub fn registerVariable(self: *Parser, name: []const u8, node: AstNode) !void {
        if (self.currentNs()) |ns| try ns.get().registerVariable(name, node);
    }

    pub fn registerFunction(self: *Parser, name: []const u8, node: AstNode) !void {
        if (self.currentNs()) |ns| try ns.get().registerFunction(name, node);
    }

    pub fn registerType(self: *Parser, name: []const u8, typ: TypeHandle) !void {
        if (self.currentNs()) |ns| try ns.get().registerType(name, typ);
    }

    pub fn registerModule(self: *Parser, name: []const u8, node: AstNode) !void {
        if (self.currentNs()) |ns| try ns.get().registerModule(name, node);
    }

    // Look up a dotted name (e.g. ["mod", "func"]) through the namespace stack.
    fn walkNamespace(self: *const Parser, names: []const []const u8) ?NsHandle {
        std.debug.assert(names.len > 0);
        if (self.namespaces.items.len == 0) return null;
        var ns = self.namespaces.items[self.namespaces.items.len - 1];
        for (names[0 .. names.len - 1]) |seg| {
            const mod = ns.getConst().findModule(seg) orelse return null;
            ns = mod.getConst().ns;
            if (ns.isNull()) return null;
        }
        return ns;
    }

    pub fn findVariable(self: *const Parser, names: []const []const u8) ?AstNode {
        const ns = self.walkNamespace(names) orelse return null;
        return ns.getConst().findVariable(names[names.len - 1]);
    }

    pub fn findType(self: *const Parser, names: []const []const u8) ?TypeHandle {
        const ns = self.walkNamespace(names) orelse return null;
        return ns.getConst().findType(names[names.len - 1]);
    }

    pub fn findFunctions(self: *const Parser, names: []const []const u8, out: *ArrayList(AstNode)) !void {
        const ns = self.walkNamespace(names) orelse return;
        try ns.getConst().findFunctions(names[names.len - 1], out);
    }

    // ── Function lookup helpers ───────────────────────────────────────────────

    // Collect all function overloads matching `names` through the namespace stack.
    pub fn findOverloads(self: *const Parser, names: []const []const u8, out: *ArrayList(AstNode)) !void {
        const ns = self.walkNamespace(names) orelse return;
        try ns.getConst().findFunctions(names[names.len - 1], out);
    }

    // Walk the namespace stack upward to find the nearest enclosing function definition.
    pub fn currentFunction(self: *const Parser) AstNode {
        var i = self.namespaces.items.len;
        while (i > 0) {
            i -= 1;
            const ns_handle = self.namespaces.items[i];
            const owner = ns_handle.getConst().node;
            if (!owner.isNull() and owner.getConst().kind() == .function_definition) {
                return owner;
            }
        }
        return AstNode.null_handle;
    }

    pub fn hasVariable(self: *const Parser, names: []const []const u8) bool {
        return self.findVariable(names) != null;
    }

    // ── Internal helpers ──────────────────────────────────────────────────────

    fn makeNumber(self: *Parser, tok: Token) !?AstNode {
        const text = self.tokenText(tok);
        const radix: Radix = if (tok.kind == .number) tok.value.radix else .decimal;
        const val = parseIntValue(text, radix) catch {
            self.appendTokenError(tok, "Invalid integer literal", .{});
            return null;
        };
        return try self.makeNode(tok.location, .{ .number = .{ .value = val } });
    }
};

// ── Numeric literal helpers (module-level) ────────────────────────────────────

pub const IntParseError = error{InvalidNumber};

pub fn parseIntValue(text: []const u8, radix: Radix) IntParseError!sn.Number.Int {
    const base: u8 = switch (radix) { .decimal => 10, .hex => 16, .binary => 2 };
    const stripped = switch (radix) {
        .decimal => text,
        .hex => if (text.len >= 2 and (text[1] == 'x' or text[1] == 'X')) text[2..] else text,
        .binary => if (text.len >= 2 and (text[1] == 'b' or text[1] == 'B')) text[2..] else text,
    };
    const v = std.fmt.parseUnsigned(u64, stripped, base) catch return error.InvalidNumber;
    if (v <= std.math.maxInt(i32)) return .{ .i32 = @intCast(v) };
    if (v <= std.math.maxInt(u32)) return .{ .u32 = @intCast(v) };
    if (v <= std.math.maxInt(i64)) return .{ .i64 = @intCast(v) };
    return .{ .u64 = v };
}

pub fn parseDecimalValue(whole: []const u8, frac: ?[]const u8, exponent: ?[]const u8) !f64 {
    var buf: [256]u8 = undefined;
    var n: usize = 0;
    @memcpy(buf[n .. n + whole.len], whole);
    n += whole.len;
    if (frac) |f| {
        buf[n] = '.'; n += 1;
        @memcpy(buf[n .. n + f.len], f);
        n += f.len;
    }
    if (exponent) |e| {
        buf[n] = 'e'; n += 1;
        @memcpy(buf[n .. n + e.len], e);
        n += e.len;
    }
    return std.fmt.parseFloat(f64, buf[0..n]) catch return error.InvalidNumber;
}
