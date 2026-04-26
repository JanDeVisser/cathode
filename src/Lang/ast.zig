// Normalize and bind pass implementations for the cathode AST.
// Replaces src/Lang/AST/*.cpp and related support files (Resolve.cpp, Fold.cpp, Coerce.cpp).
// Called from the compilation pipeline — not imported by parser.zig (avoids circular dep).
const std = @import("std");
const ArrayList = std.array_list.Managed;
const Parser = @import("parser.zig").Parser;
const sn = @import("syntax_node.zig");
const AstNode = sn.AstNode;
const AstNodeImpl = sn.AstNodeImpl;
const SyntaxNode = sn.SyntaxNode;
const AstStatus = sn.AstStatus;
const BindResult = sn.BindResult;
const NsHandle = sn.NsHandle;
const Namespace = sn.Namespace;
const Visibility = sn.Visibility;
const type_mod = @import("type.zig");
const TypeHandle = type_mod.TypeHandle;
const TypeKind = type_mod.TypeKind;
const op_mod = @import("operator.zig");
const Operator = op_mod.Operator;

// ── assign-op mapping (mirrors Operator.cpp) ──────────────────────────────────

fn assignOpBase(op: Operator) ?Operator {
    return switch (op) {
        .assign_and       => .binary_and,
        .assign_decrement => .subtract,
        .assign_divide    => .divide,
        .assign_increment => .add,
        .assign_modulo    => .modulo,
        .assign_multiply  => .multiply,
        .assign_or        => .binary_or,
        .assign_shift_left  => .shift_left,
        .assign_shift_right => .shift_right,
        .assign_xor       => .binary_xor,
        else => null,
    };
}

// ── Cardinality: number of distinct values a type can hold ────────────────────

pub fn cardinality(t: TypeHandle) ?usize {
    if (t.isNull()) return null;
    return switch (t.getConst().description) {
        .enum_type => |e| e.values.len,
        .tagged_union_type => |tu| cardinality(tu.tag_type),
        .optional_type => |o| if (cardinality(o.type)) |c| c + 1 else null,
        .array => |a| if (cardinality(a.array_of)) |c| std.math.pow(usize, c, a.size) else null,
        .reference_type => |r| cardinality(r.referencing),
        else => null,
    };
}

// ── isConstant ────────────────────────────────────────────────────────────────

fn isConstant(node: AstNode) bool {
    if (node.isNull()) return false;
    return switch (node.getConst().node) {
        .variable_declaration => |v| v.is_const,
        else => false,
    };
}

// ── typeHandleFor: map Number.Int tag to builtin TypeHandle ───────────────────

fn typeHandleForNumber(v: sn.Number.Int) TypeHandle {
    return switch (v) {
        .u64 => type_mod.the().u64_type,
        .i64 => type_mod.the().i64_type,
        .u32 => type_mod.the().u32_type,
        .i32 => type_mod.the().i32_type,
        .u16 => type_mod.the().u16_type,
        .i16 => type_mod.the().i16_type,
        .u8  => type_mod.the().u8_type,
        .i8  => type_mod.the().i8_type,
    };
}

// ── resolveTypeSpec: TypeSpecification → TypeHandle ───────────────────────────
// Returns null when a name is not yet in scope (deferred to next bind pass).

pub fn resolveTypeSpec(parser: *const Parser, node: AstNode) ?TypeHandle {
    if (node.isNull()) return null;
    const ts = switch (node.getConst().node) {
        .type_specification => |ts| ts,
        else => return null,
    };
    return switch (ts.description) {
        .type_name => |d| blk: {
            var t = parser.findType(d.name) orelse break :blk null;
            // Unwrap type aliases
            while (t.getConst().description == .type_alias) {
                t = t.getConst().description.type_alias.alias_of;
            }
            break :blk t;
        },
        .reference => |d| blk: {
            const inner = resolveTypeSpec(parser, d.referencing) orelse break :blk null;
            break :blk type_mod.the().referencing(inner) catch null;
        },
        .slice => |d| blk: {
            const inner = resolveTypeSpec(parser, d.slice_of) orelse break :blk null;
            break :blk type_mod.the().sliceOf(inner) catch null;
        },
        .zero_terminated_array => |d| blk: {
            const inner = resolveTypeSpec(parser, d.array_of) orelse break :blk null;
            break :blk type_mod.the().zeroTerminatedArrayOf(inner) catch null;
        },
        .array => |d| blk: {
            const inner = resolveTypeSpec(parser, d.array_of) orelse break :blk null;
            break :blk type_mod.the().arrayOf(inner, d.size) catch null;
        },
        .dyn_array => |d| blk: {
            const inner = resolveTypeSpec(parser, d.array_of) orelse break :blk null;
            break :blk type_mod.the().dynArrayOf(inner) catch null;
        },
        .optional => |d| blk: {
            const inner = resolveTypeSpec(parser, d.optional_of) orelse break :blk null;
            break :blk type_mod.the().optionalOf(inner) catch null;
        },
        .pointer => |d| blk: {
            const inner = resolveTypeSpec(parser, d.referencing) orelse break :blk null;
            break :blk type_mod.the().pointerTo(inner) catch null;
        },
        .result => |d| blk: {
            const s = resolveTypeSpec(parser, d.success) orelse break :blk null;
            const e = resolveTypeSpec(parser, d.@"error") orelse break :blk null;
            break :blk type_mod.the().resultOf(s, e) catch null;
        },
    };
}

// ── bindError helpers ─────────────────────────────────────────────────────────

fn bindError(
    parser: *Parser,
    node: AstNode,
    comptime fmt: []const u8,
    args: anytype,
) BindResult {
    parser.appendError(node.getConst().location, fmt, args);
    node.get().status = .bind_errors;
    return error.BindErrors;
}

fn bindErrorAt(
    parser: *Parser,
    loc: sn.AstNode,
    comptime fmt: []const u8,
    args: anytype,
) BindResult {
    parser.appendError(loc.getConst().location, fmt, args);
    return error.BindErrors;
}

// ── normalize ─────────────────────────────────────────────────────────────────

pub fn normalize(parser: *Parser, node: AstNode) anyerror!AstNode {
    if (node.isNull()) return node;
    const status = node.getConst().status;
    if (status != .initialized) return node;
    const result = try dispatchNormalize(parser, node);
    if (!AstNode.eql(result, node)) {
        // result supersedes node
        if (result.getConst().status == .initialized) {
            result.get().status = .normalized;
        }
    } else {
        node.get().status = .normalized;
    }
    return result;
}

pub fn normalizeNodes(parser: *Parser, nodes: []AstNode) ![]AstNode {
    const alloc = parser.arena.allocator();
    const out = try alloc.alloc(AstNode, nodes.len);
    for (nodes, 0..) |n, i| out[i] = try normalize(parser, n);
    return out;
}

fn dispatchNormalize(parser: *Parser, node: AstNode) !AstNode {
    return switch (node.getConst().node) {
        // Nodes that just recursively normalize their children:
        .alias         => normalizeAlias(parser, node),
        .argument_list => normalizeArgumentList(parser, node),
        .block         => normalizeBlock(parser, node),
        .@"break"      => normalizeBreak(parser, node),
        .defer_statement => normalizeDeferStatement(parser, node),
        .@"enum"       => normalizeEnum(parser, node),
        .enum_value    => normalizeEnumValue(parser, node),
        .for_statement => normalizeForStatement(parser, node),
        .function_declaration => normalizeFunctionDeclaration(parser, node),
        .function_definition  => normalizeFunctionDefinition(parser, node),
        .if_statement  => normalizeIfStatement(parser, node),
        .loop_statement => normalizeLoopStatement(parser, node),
        .parameter     => normalizeParameter(parser, node),
        .@"return"     => normalizeReturn(parser, node),
        .@"struct"     => normalizeStruct(parser, node),
        .struct_member => normalizeStructMember(parser, node),
        .switch_case   => normalizeSwitchCase(parser, node),
        .switch_statement => normalizeSwitchStatement(parser, node),
        .type_specification => normalizeTypeSpecification(parser, node),
        .while_statement => normalizeWhileStatement(parser, node),
        .yield         => normalizeYield(parser, node),
        // Expression nodes:
        .binary_expression => normalizeBinaryExpression(parser, node),
        .expression_list   => normalizeExpressionList(parser, node),
        .unary_expression  => normalizeUnaryExpression(parser, node),
        // Quoted string transforms into String/CString/Number:
        .quoted_string => normalizeQuotedString(parser, node),
        // Module-level nodes:
        .export_declaration  => normalizeExportDeclaration(parser, node),
        .public_declaration  => normalizePublicDeclaration(parser, node),
        .import        => normalizeImport(parser, node),
        .include       => normalizeInclude(parser, node),
        .@"extern"     => normalizeExtern(parser, node),
        .embed         => normalizeEmbed(parser, node),
        .comptime_node => normalizeComptime(parser, node),
        .module        => normalizeModule(parser, node),
        .program       => normalizeProgram(parser, node),
        // Already-leaf nodes — no children to normalize:
        else => node,
    };
}

// ── normalize implementations ─────────────────────────────────────────────────

fn normalizeAlias(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.alias;
    const aliased = try normalize(parser, n.aliased_type);
    return parser.deriveNode(node, .{ .alias = .{ .name = n.name, .aliased_type = aliased } });
}

fn normalizeArgumentList(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.argument_list;
    const args = try normalizeNodes(parser, n.arguments);
    return parser.deriveNode(node, .{ .argument_list = .{ .arguments = args } });
}

fn normalizeBlock(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.block;
    const stmts = try normalizeNodes(parser, n.statements);
    return parser.deriveNode(node, .{ .block = .{ .statements = stmts, .label = n.label } });
}

fn normalizeBreak(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.@"break";
    // Walk namespace stack to find the target block
    var i = parser.namespaces.items.len;
    while (i > 0) {
        i -= 1;
        const ns_handle = parser.namespaces.items[i];
        const owner = ns_handle.getConst().node;
        if (owner.isNull()) continue;
        const matches = switch (owner.getConst().node) {
            .block, .if_statement, .while_statement, .for_statement,
            .loop_statement, .switch_statement => blk: {
                if (n.label) |lbl| {
                    const owner_label: ?[]const u8 = switch (owner.getConst().node) {
                        .block => |b| b.label,
                        .if_statement => |s| s.label,
                        .while_statement => |s| s.label,
                        .for_statement => |s| s.label,
                        .loop_statement => |s| s.label,
                        .switch_statement => |s| s.label,
                        else => null,
                    };
                    break :blk if (owner_label) |ol| std.mem.eql(u8, ol, lbl) else false;
                }
                break :blk true;
            },
            else => false,
        };
        if (matches) {
            return parser.deriveNode(node, .{ .@"break" = .{ .label = n.label, .block = owner } });
        }
    }
    if (n.label) |lbl| {
        parser.appendError(node.getConst().location, "Block '{s}' not found", .{lbl});
    } else {
        parser.appendError(node.getConst().location, "`break` statement not inside a breakable block", .{});
    }
    return node;
}

fn normalizeDeferStatement(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.defer_statement;
    const stmt = try normalize(parser, n.statement);
    return parser.deriveNode(node, .{ .defer_statement = .{ .statement = stmt } });
}

fn normalizeEnum(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.@"enum";
    const values = try normalizeNodes(parser, n.values);
    return parser.deriveNode(node, .{ .@"enum" = .{
        .name = n.name,
        .underlying_type = try normalize(parser, n.underlying_type),
        .values = values,
    } });
}

fn normalizeEnumValue(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.enum_value;
    return parser.deriveNode(node, .{ .enum_value = .{
        .label = n.label,
        .value = try normalize(parser, n.value),
        .payload = try normalize(parser, n.payload),
    } });
}

fn normalizeForStatement(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.for_statement;
    return parser.deriveNode(node, .{ .for_statement = .{
        .range_variable = n.range_variable,
        .range_expr = try normalize(parser, n.range_expr),
        .statement = try normalize(parser, n.statement),
        .label = n.label,
    } });
}

fn normalizeFunctionDeclaration(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.function_declaration;
    return parser.deriveNode(node, .{ .function_declaration = .{
        .name = n.name,
        .generics = try normalizeNodes(parser, n.generics),
        .parameters = try normalizeNodes(parser, n.parameters),
        .return_type = try normalize(parser, n.return_type),
    } });
}

fn normalizeFunctionDefinition(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.function_definition;
    return parser.deriveNode(node, .{ .function_definition = .{
        .name = n.name,
        .declaration = try normalize(parser, n.declaration),
        .implementation = try normalize(parser, n.implementation),
        .visibility = n.visibility,
    } });
}

fn normalizeIfStatement(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.if_statement;
    return parser.deriveNode(node, .{ .if_statement = .{
        .condition = try normalize(parser, n.condition),
        .if_branch = try normalize(parser, n.if_branch),
        .else_branch = try normalize(parser, n.else_branch),
        .label = n.label,
    } });
}

fn normalizeLoopStatement(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.loop_statement;
    return parser.deriveNode(node, .{ .loop_statement = .{
        .label = n.label,
        .statement = try normalize(parser, n.statement),
    } });
}

fn normalizeParameter(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.parameter;
    return parser.deriveNode(node, .{ .parameter = .{
        .name = n.name,
        .type_name = try normalize(parser, n.type_name),
    } });
}

fn normalizeReturn(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.@"return";
    return parser.deriveNode(node, .{ .@"return" = .{
        .expression = try normalize(parser, n.expression),
    } });
}

fn normalizeStruct(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.@"struct";
    return parser.deriveNode(node, .{ .@"struct" = .{
        .name = n.name,
        .members = try normalizeNodes(parser, n.members),
    } });
}

fn normalizeStructMember(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.struct_member;
    return parser.deriveNode(node, .{ .struct_member = .{
        .label = n.label,
        .member_type = try normalize(parser, n.member_type),
    } });
}

fn normalizeSwitchCase(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.switch_case;
    // Transform `_` identifiers in case values to DefaultSwitchValue
    const norm_case_val = blk: {
        const cv = try normalize(parser, n.case_value);
        switch (cv.getConst().node) {
            .expression_list => |list| {
                var new_exprs = ArrayList(AstNode).init(parser.arena.allocator());
                for (list.expressions) |e| {
                    if (e.getConst().node == .identifier and
                        std.mem.eql(u8, e.getConst().node.identifier.identifier, "_"))
                    {
                        try new_exprs.append(try normalize(parser,
                            try parser.makeNode(e.getConst().location, .{ .default_switch_value = .{} })));
                    } else {
                        try new_exprs.append(e);
                    }
                }
                break :blk try parser.deriveNode(cv, .{ .expression_list = .{
                    .expressions = try new_exprs.toOwnedSlice(),
                } });
            },
            .identifier => |id| {
                if (std.mem.eql(u8, id.identifier, "_")) {
                    break :blk try normalize(parser,
                        try parser.makeNode(cv.getConst().location, .{ .default_switch_value = .{} }));
                }
                break :blk cv;
            },
            else => break :blk cv,
        }
    };
    return parser.deriveNode(node, .{ .switch_case = .{
        .case_value = norm_case_val,
        .binding = try normalize(parser, n.binding),
        .statement = try normalize(parser, n.statement),
    } });
}

fn normalizeSwitchStatement(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.switch_statement;
    return parser.deriveNode(node, .{ .switch_statement = .{
        .label = n.label,
        .switch_value = try normalize(parser, n.switch_value),
        .switch_cases = try normalizeNodes(parser, n.switch_cases),
    } });
}

fn normalizeTypeSpecification(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.type_specification;
    const new_desc: sn.TypeSpecification.Description = switch (n.description) {
        .type_name => |d| .{ .type_name = .{
            .name = d.name,
            .arguments = try normalizeNodes(parser, d.arguments),
        } },
        .reference => |d| .{ .reference = .{ .referencing = try normalize(parser, d.referencing) } },
        .slice => |d| .{ .slice = .{ .slice_of = try normalize(parser, d.slice_of) } },
        .zero_terminated_array => |d| .{ .zero_terminated_array = .{ .array_of = try normalize(parser, d.array_of) } },
        .array => |d| .{ .array = .{ .array_of = try normalize(parser, d.array_of), .size = d.size } },
        .dyn_array => |d| .{ .dyn_array = .{ .array_of = try normalize(parser, d.array_of) } },
        .optional => |d| .{ .optional = .{ .optional_of = try normalize(parser, d.optional_of) } },
        .pointer => |d| .{ .pointer = .{ .referencing = try normalize(parser, d.referencing) } },
        .result => |d| .{ .result = .{
            .success = try normalize(parser, d.success),
            .@"error" = try normalize(parser, d.@"error"),
        } },
    };
    return parser.deriveNode(node, .{ .type_specification = .{ .description = new_desc } });
}

fn normalizeWhileStatement(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.while_statement;
    return parser.deriveNode(node, .{ .while_statement = .{
        .label = n.label,
        .condition = try normalize(parser, n.condition),
        .statement = try normalize(parser, n.statement),
    } });
}

fn normalizeYield(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.yield;
    return parser.deriveNode(node, .{ .yield = .{
        .label = n.label,
        .statement = try normalize(parser, n.statement),
    } });
}

fn normalizeExpressionList(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.expression_list;
    return parser.deriveNode(node, .{ .expression_list = .{
        .expressions = try normalizeNodes(parser, n.expressions),
    } });
}

fn normalizeUnaryExpression(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.unary_expression;
    const norm_op = try normalize(parser, n.operand);
    if (foldUnary(n.op, norm_op)) |folded| return folded;
    return parser.deriveNode(node, .{ .unary_expression = .{ .op = n.op, .operand = norm_op } });
}

// Fold constant unary expressions.
fn foldUnary(op: Operator, operand: AstNode) ?AstNode {
    _ = op; _ = operand;
    return null; // constant folding for unary not yet implemented
}

// Fold constant binary expressions (e.g. 2+3 → 5).
fn foldBinary(op: Operator, lhs: AstNode, rhs: AstNode) ?AstNode {
    _ = op; _ = lhs; _ = rhs;
    return null; // constant folding for binary not yet implemented
}

fn normalizeBinaryExpression(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.binary_expression;

    // Compound-assign operators: a op= b → a = a op b
    if (assignOpBase(n.op)) |base_op| {
        const lhs = try normalize(parser, n.lhs);
        const rhs = try normalize(parser, n.rhs);
        const bin = try parser.makeNode(node.getConst().location,
            .{ .binary_expression = .{ .lhs = lhs, .op = base_op, .rhs = rhs } });
        return parser.deriveNode(node, .{ .binary_expression = .{
            .lhs = n.lhs, .op = .assign, .rhs = bin,
        } });
    }

    switch (n.op) {
        .call => {
            // lhs(rhs) → Call node
            const lhs = try normalize(parser, n.lhs);
            var arg_list = try normalize(parser, n.rhs);

            // Normalize argument list shape
            if (arg_list.getConst().node == .void_node) {
                arg_list = try parser.makeNode(arg_list.getConst().location, .{ .argument_list = .{ .arguments = &.{} } });
                arg_list.get().status = .normalized;
            } else if (arg_list.getConst().node != .expression_list) {
                const exprs = try parser.arena.allocator().dupe(AstNode, &[_]AstNode{arg_list});
                arg_list = try parser.makeNode(arg_list.getConst().location, .{ .argument_list = .{ .arguments = exprs } });
                arg_list.get().status = .normalized;
            } else {
                const exprs = arg_list.getConst().node.expression_list.expressions;
                arg_list = try parser.makeNode(arg_list.getConst().location, .{ .argument_list = .{ .arguments = exprs } });
                arg_list = try normalize(parser, arg_list);
            }

            // Build the callable name (identifier or dotted)
            const callable = try makeName(parser, lhs) orelse lhs;
            const call_node = try parser.makeNode(node.getConst().location, .{ .call = .{
                .callable = callable,
                .arguments = arg_list,
                .function = AstNode.null_handle,
            } });
            return normalize(parser, call_node);
        },
        .sequence => {
            // Flatten sequence into ExpressionList
            var list = ArrayList(AstNode).init(parser.arena.allocator());
            try flattenSequence(parser, node, &list);
            const slice = try list.toOwnedSlice();
            return parser.deriveNode(node, .{ .expression_list = .{ .expressions = slice } });
        },
        .member_access => {
            const lhs = try normalize(parser, n.lhs);
            var rhs = try normalize(parser, n.rhs);
            // If rhs is a quoted string, convert it to an identifier
            if (rhs.getConst().node == .quoted_string) {
                const qs = rhs.getConst().node.quoted_string;
                rhs = try parser.makeNode(rhs.getConst().location, .{ .identifier = .{ .identifier = qs.string } });
                rhs.get().status = .normalized;
            }
            return parser.deriveNode(node, .{ .binary_expression = .{
                .lhs = lhs, .op = .member_access, .rhs = rhs,
            } });
        },
        .range => {
            const lhs = try normalize(parser, n.lhs);
            const rhs = try normalize(parser, n.rhs);
            return parser.deriveNode(node, .{ .binary_expression = .{ .lhs = lhs, .op = .range, .rhs = rhs } });
        },
        else => {
            const lhs = try normalize(parser, n.lhs);
            const rhs = try normalize(parser, n.rhs);
            if (foldBinary(n.op, lhs, rhs)) |folded| return folded;
            return parser.deriveNode(node, .{ .binary_expression = .{ .lhs = lhs, .op = n.op, .rhs = rhs } });
        },
    }
}

fn flattenSequence(parser: *Parser, node: AstNode, list: *ArrayList(AstNode)) !void {
    switch (node.getConst().node) {
        .binary_expression => |n| {
            if (n.op == .sequence) {
                try flattenSequence(parser, n.lhs, list);
                try list.append(try normalize(parser, n.rhs));
            } else {
                try list.append(try normalize(parser, node));
            }
        },
        else => try list.append(try normalize(parser, node)),
    }
}

// Build a name node (Identifier or IdentifierList) from an expression.
fn makeName(parser: *Parser, node: AstNode) !?AstNode {
    return switch (node.getConst().node) {
        .identifier => node,
        .stamped_identifier => node,
        .binary_expression => |n| {
            if (n.op != .member_access) return null;
            var parts = ArrayList([]const u8).init(parser.arena.allocator());
            if (!try collectDottedName(node, &parts)) return null;
            const slice = try parts.toOwnedSlice();
            return try parser.makeNode(node.getConst().location, .{ .identifier_list = .{ .identifiers = slice } });
        },
        else => null,
    };
}

fn collectDottedName(node: AstNode, parts: *ArrayList([]const u8)) !bool {
    switch (node.getConst().node) {
        .identifier => |id| {
            try parts.append(id.identifier);
            return true;
        },
        .binary_expression => |n| {
            if (n.op != .member_access) return false;
            if (!try collectDottedName(n.lhs, parts)) return false;
            if (!try collectDottedName(n.rhs, parts)) return false;
            return true;
        },
        else => return false,
    }
}

fn normalizeQuotedString(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.quoted_string;
    // Strip escape sequences from the content
    const unescaped = try unescape(parser.arena.allocator(), n.string);
    return switch (n.quote_type) {
        .double => parser.deriveNode(node, .{ .string = .{ .string = unescaped } }),
        .backtick => parser.deriveNode(node, .{ .cstring = .{ .string = unescaped } }),
        .single => blk: {
            // Single-quote → u64 codepoint
            if (unescaped.len == 0) {
                parser.appendError(node.getConst().location, "Empty character literal", .{});
                break :blk node;
            }
            const cp: u64 = unescaped[0];
            break :blk parser.deriveNode(node, .{ .number = .{ .value = .{ .u64 = cp } } });
        },
    };
}

fn unescape(allocator: std.mem.Allocator, s: []const u8) ![]const u8 {
    var out = ArrayList(u8).init(allocator);
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        if (s[i] == '\\' and i + 1 < s.len) {
            i += 1;
            try out.append(switch (s[i]) {
                'n' => '\n',
                'r' => '\r',
                't' => '\t',
                '0' => 0,
                else => s[i],
            });
        } else {
            try out.append(s[i]);
        }
    }
    return out.toOwnedSlice();
}

fn normalizeExportDeclaration(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.export_declaration;
    var decl = try normalize(parser, n.declaration);
    setVisibility(&decl, .export_vis);
    return decl;
}

fn normalizePublicDeclaration(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.public_declaration;
    var decl = try normalize(parser, n.declaration);
    setVisibility(&decl, .public);
    return decl;
}

fn setVisibility(node: *AstNode, vis: Visibility) void {
    switch (node.get().node) {
        .function_definition => |*fd| fd.visibility = vis,
        .variable_declaration => |*vd| vd.visibility = vis,
        else => {},
    }
}

fn normalizeImport(parser: *Parser, node: AstNode) !AstNode {
    // Import resolution is deferred — file I/O happens in the compilation pipeline.
    // For now, return the import node unchanged; the pipeline will process imports.
    _ = parser;
    return node;
}

fn normalizeInclude(parser: *Parser, node: AstNode) !AstNode {
    _ = parser;
    return node; // File I/O handled by compilation pipeline
}

fn normalizeExtern(parser: *Parser, node: AstNode) !AstNode {
    const n = &node.getConst().node.@"extern";
    var normalized_decls = ArrayList(AstNode).init(parser.arena.allocator());
    for (n.declarations) |decl| {
        if (decl.getConst().node == .function_declaration) {
            const fn_name = decl.getConst().node.function_declaration.name;
            const link_name = try std.fmt.allocPrint(
                parser.arena.allocator(), "{s}:{s}", .{ n.library, fn_name });
            const ext_link = try parser.makeNode(decl.getConst().location, .{
                .extern_link = .{ .link_name = link_name },
            });
            const fn_def = try parser.makeNode(decl.getConst().location, .{ .function_definition = .{
                .name = fn_name,
                .declaration = decl,
                .implementation = ext_link,
            } });
            try normalized_decls.append(try normalize(parser, fn_def));
        } else {
            try normalized_decls.append(try normalize(parser, decl));
        }
    }
    return parser.deriveNode(node, .{ .@"extern" = .{
        .declarations = try normalized_decls.toOwnedSlice(),
        .library = n.library,
    } });
}

fn normalizeEmbed(_: *Parser, node: AstNode) !AstNode {
    return node; // File I/O handled by compilation pipeline
}

fn normalizeComptime(_: *Parser, node: AstNode) !AstNode {
    return node; // Comptime execution requires QBE (Session 5)
}

fn normalizeModule(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    const n = &node.getConst().node.module;
    const stmts = try normalizeNodes(parser, n.statements);
    return parser.deriveNode(node, .{ .module = .{
        .name = n.name,
        .source = n.source,
        .statements = stmts,
    } });
}

fn normalizeProgram(parser: *Parser, node: AstNode) !AstNode {
    try parser.initNamespace(node);
    // Register all built-in types in the program namespace
    const reg = type_mod.the();
    for (reg.types.items) |t| {
        if (!t.id.isNull()) {
            parser.registerType(t.name, t.id) catch {};
        }
    }
    const n = &node.getConst().node.program;
    const stmts = try normalizeNodes(parser, n.statements);
    return parser.deriveNode(node, .{ .program = .{
        .name = n.name,
        .source = n.source,
        .statements = stmts,
    } });
}

// ── bind ──────────────────────────────────────────────────────────────────────

pub fn bindNode(parser: *Parser, node: AstNode) BindResult {
    if (node.isNull()) return type_mod.the().void_type;
    const status = node.getConst().status;
    if (status == .bound) return node.getConst().bound_type;
    if (status == .bind_errors) return error.BindErrors;
    if (status == .ambiguous) return error.Ambiguous;
    if (status == .internal_error) return error.InternalError;

    const result = dispatchBind(parser, node);
    if (result) |t| {
        node.get().bound_type = t;
        node.get().status = .bound;
    } else |err| {
        node.get().status = switch (err) {
            error.Undetermined => .undetermined,
            error.Ambiguous    => .ambiguous,
            error.BindErrors   => .bind_errors,
            error.InternalError, error.OutOfMemory => .internal_error,
        };
    }
    return result;
}

pub fn tryBind(parser: *Parser, node: AstNode) !TypeHandle {
    const result = bindNode(parser, node);
    return result catch |err| switch (err) {
        error.Undetermined => {
            parser.unbound += 1;
            return type_mod.the().void_type;
        },
        error.Ambiguous, error.BindErrors, error.InternalError, error.OutOfMemory => return err,
    };
}

pub fn tryBindNodes(parser: *Parser, nodes: []AstNode) ![]TypeHandle {
    const alloc = parser.arena.allocator();
    const out = try alloc.alloc(TypeHandle, nodes.len);
    for (nodes, 0..) |n, i| {
        out[i] = try tryBind(parser, n);
    }
    return out;
}

fn dispatchBind(parser: *Parser, node: AstNode) BindResult {
    return switch (node.getConst().node) {
        .dummy            => type_mod.the().void_type,
        .void_node        => bindVoid(parser, node),
        .bool_constant    => bindBoolConstant(parser, node),
        .cstring          => type_mod.the().cstring,
        .decimal          => type_mod.the().f64_type,
        .null_ptr         => type_mod.the().void_type,
        .number           => bindNumber(parser, node),
        .string           => type_mod.the().string,
        .extern_link      => type_mod.the().void_type,
        .default_switch_value => type_mod.the().void_type,
        .quoted_string    => type_mod.the().string, // should not reach here after normalize
        .identifier       => bindIdentifier(parser, node),
        .variable_declaration => bindVariableDeclaration(parser, node),
        .alias            => bindAlias(parser, node),
        .@"enum"          => bindEnum(parser, node),
        .enum_value       => type_mod.the().void_type,
        .@"struct"        => bindStruct(parser, node),
        .struct_member    => bindStructMember(parser, node),
        .type_specification => bindTypeSpecification(parser, node),
        .module           => bindModule(parser, node),
        .module_proxy     => type_mod.the().void_type,
        .program          => bindProgram(parser, node),
        .import           => type_mod.the().void_type,
        .include          => type_mod.the().void_type,
        .embed            => type_mod.the().void_type,
        .comptime_node    => type_mod.the().void_type,
        .export_declaration  => type_mod.the().void_type,
        .public_declaration  => type_mod.the().void_type,
        .@"extern"        => bindExtern(parser, node),
        .argument_list    => bindArgumentList(parser, node),
        .call             => bindCall(parser, node),
        .function_declaration  => bindFunctionDeclaration(parser, node),
        .function_definition   => bindFunctionDefinition(parser, node),
        .parameter        => bindParameter(parser, node),
        .block            => bindBlock(parser, node),
        .@"break"         => type_mod.the().void_type,
        .@"continue"      => type_mod.the().void_type,
        .@"return"        => bindReturn(parser, node),
        .defer_statement  => bindDeferStatement(parser, node),
        .for_statement    => bindForStatement(parser, node),
        .if_statement     => bindIfStatement(parser, node),
        .loop_statement   => bindLoopStatement(parser, node),
        .while_statement  => bindWhileStatement(parser, node),
        .yield            => bindYield(parser, node),
        .switch_case      => bindSwitchCase(parser, node),
        .switch_statement => bindSwitchStatement(parser, node),
        .binary_expression => bindBinaryExpression(parser, node),
        .expression_list   => bindExpressionList(parser, node),
        .unary_expression  => bindUnaryExpression(parser, node),
        .identifier_list   => type_mod.the().void_type,
        .stamped_identifier => type_mod.the().void_type,
        .tag_value        => |tv| tv.payload_type,
    };
}

// ── bind implementations ──────────────────────────────────────────────────────

fn bindVoid(parser: *Parser, node: AstNode) BindResult {
    return type_mod.the().typeOf(type_mod.the().void_type) catch {
        return bindError(parser, node, "internal: typeOf(void) failed", .{});
    };
}

fn bindBoolConstant(_: *Parser, _: AstNode) BindResult {
    return type_mod.the().boolean;
}

fn bindNumber(_: *Parser, node: AstNode) BindResult {
    return typeHandleForNumber(node.getConst().node.number.value);
}

fn bindIdentifier(parser: *Parser, node: AstNode) BindResult {
    const id = node.getConst().node.identifier.identifier;
    // Look up in namespace stack
    const t = parser.findType(&[_][]const u8{id});
    if (t) |found| {
        return type_mod.the().typeOf(found) catch error.InternalError;
    }
    const vtype = if (parser.findVariable(&[_][]const u8{id})) |v| v.getConst().bound_type else TypeHandle.null_handle;
    if (!vtype.isNull()) {
        // Check if it's a constant - if so set superceded_by to its initializer
        if (parser.findVariable(&[_][]const u8{id})) |v| {
            if (isConstant(v) and v.getConst().node == .variable_declaration) {
                const init_node = v.getConst().node.variable_declaration.initializer;
                if (!init_node.isNull()) node.get().superceded_by = init_node;
            }
        }
        return vtype;
    }
    // Check if it's a module
    if (parser.currentNs()) |ns| {
        if (ns.getConst().findModule(id)) |proxy| {
            const mod_proxy = parser.makeNode(node.getConst().location, .{ .module_proxy = .{
                .name = id,
                .module = proxy.getConst().node.module_proxy.module,
            } }) catch return error.InternalError;
            node.get().superceded_by = mod_proxy;
            return type_mod.the().module;
        }
    }
    if (parser.pass == 0) return error.Undetermined;
    return bindError(parser, node, "Unresolved identifier '{s}'", .{id});
}

fn bindVariableDeclaration(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.variable_declaration;
    var my_type: TypeHandle = TypeHandle.null_handle;
    var init_type: TypeHandle = TypeHandle.null_handle;

    if (!n.type_name.isNull()) {
        const tt = try tryBind(parser, n.type_name);
        if (tt.getConst().description == .type_type) {
            my_type = tt.getConst().description.type_type.type;
        } else {
            my_type = tt;
        }
    }
    if (!n.initializer.isNull()) {
        init_type = try tryBind(parser, n.initializer);
        if (init_type.getConst().description == .type_type) {
            init_type = init_type.getConst().description.type_type.type;
        }
    }

    if (my_type.isNull() and init_type.isNull()) return error.Undetermined;
    if (my_type.isNull()) my_type = init_type;

    if (!init_type.isNull() and !TypeHandle.eql(my_type, init_type)) {
        if (!init_type.getConst().compatible(my_type) and !init_type.getConst().assignable_to(my_type)) {
            return bindError(parser, node,
                "Type mismatch: declared '{s}' but initializer is '{s}'",
                .{ my_type.getConst().name, init_type.getConst().name });
        }
    }
    if (parser.hasVariable(&[_][]const u8{n.name})) {
        return bindError(parser, node, "Duplicate variable '{s}'", .{n.name});
    }
    try parser.registerVariable(n.name, node);
    return my_type;
}

fn bindAlias(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.alias;
    _ = try tryBind(parser, n.aliased_type);
    const bound = n.aliased_type.getConst().bound_type;
    if (bound.isNull()) return error.Undetermined;
    const aliased = if (bound.getConst().description == .type_type)
        bound.getConst().description.type_type.type
    else
        bound;
    const alias_type = try type_mod.the().aliasFor(aliased);
    const named_type = try type_mod.the().namedType(n.name, .{ .type_alias = .{ .alias_of = aliased } });
    try parser.registerType(n.name, named_type);
    _ = alias_type;
    return type_mod.the().typeOf(named_type) catch error.InternalError;
}

fn bindEnum(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.@"enum";
    const reg = type_mod.the();

    const is_tagged = for (n.values) |v| {
        if (!v.getConst().node.enum_value.payload.isNull()) break true;
    } else false;

    var underlying: TypeHandle = reg.i32_type;
    if (!n.underlying_type.isNull()) {
        _ = try tryBind(parser, n.underlying_type);
        const bt = n.underlying_type.getConst().bound_type;
        if (!bt.isNull()) {
            underlying = if (bt.getConst().description == .type_type)
                bt.getConst().description.type_type.type
            else
                bt;
        }
    }

    // Build enum values
    var enum_values = ArrayList(type_mod.EnumValue).init(parser.arena.allocator());
    var cur_val: i64 = -1;
    for (n.values) |v| {
        const ev = &v.getConst().node.enum_value;
        if (!ev.value.isNull()) {
            // Already parsed as a Number node
            cur_val = switch (v.getConst().node.enum_value.value.getConst().node.number.value) {
                .i64 => |x| x,
                .u64 => |x| @intCast(x),
                .i32 => |x| x,
                .u32 => |x| x,
                .i16 => |x| x,
                .u16 => |x| x,
                .i8  => |x| x,
                .u8  => |x| x,
            };
        } else {
            cur_val += 1;
        }
        try enum_values.append(.{ .label = ev.label, .value = cur_val });
    }

    if (!is_tagged) {
        const enum_type = try reg.namedType(n.name, .{ .enum_type = .{
            .underlying_type = underlying,
            .values = try parser.arena.allocator().dupe(type_mod.EnumValue, enum_values.items),
        } });
        try parser.registerType(n.name, enum_type);
        return reg.typeOf(enum_type) catch error.InternalError;
    }

    // Tagged union
    const internal_enum_name = try std.fmt.allocPrint(
        parser.arena.allocator(), "$enum${s}", .{n.name});
    const enum_type = try reg.namedType(internal_enum_name, .{ .enum_type = .{
        .underlying_type = underlying,
        .values = try parser.arena.allocator().dupe(type_mod.EnumValue, enum_values.items),
    } });

    var tags = ArrayList(type_mod.UnionTag).init(parser.arena.allocator());
    for (n.values, enum_values.items) |v, ev| {
        const payload_node = v.getConst().node.enum_value.payload;
        var payload_type = reg.void_type;
        if (!payload_node.isNull()) {
            _ = try tryBind(parser, payload_node);
            const pt = payload_node.getConst().bound_type;
            if (!pt.isNull()) {
                payload_type = if (pt.getConst().description == .type_type)
                    pt.getConst().description.type_type.type
                else
                    pt;
            }
        }
        try tags.append(.{ .value = ev.value, .payload = payload_type });
    }

    const tagged_type = try reg.namedType(n.name, .{ .tagged_union_type = .{
        .tag_type = enum_type,
        .tags = try parser.arena.allocator().dupe(type_mod.UnionTag, tags.items),
    } });
    try parser.registerType(n.name, tagged_type);
    return reg.typeOf(tagged_type) catch error.InternalError;
}

fn bindStruct(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.@"struct";
    var fields = ArrayList(type_mod.StructField).init(parser.arena.allocator());
    for (n.members) |m| {
        _ = try tryBind(parser, m);
        const ft = m.getConst().bound_type;
        if (ft.isNull()) return error.Undetermined;
        try fields.append(.{ .name = m.getConst().node.struct_member.label, .type = ft });
    }
    const struct_type = try type_mod.the().namedType(n.name, .{ .struct_type = .{
        .fields = try parser.arena.allocator().dupe(type_mod.StructField, fields.items),
    } });
    try parser.registerType(n.name, struct_type);
    return type_mod.the().typeOf(struct_type) catch error.InternalError;
}

fn bindStructMember(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.struct_member;
    _ = try tryBind(parser, n.member_type);
    const bt = n.member_type.getConst().bound_type;
    if (bt.isNull()) return error.Undetermined;
    return if (bt.getConst().description == .type_type)
        bt.getConst().description.type_type.type
    else
        bt;
}

fn bindTypeSpecification(parser: *Parser, node: AstNode) BindResult {
    const resolved = resolveTypeSpec(parser, node) orelse return error.Undetermined;
    return type_mod.the().typeOf(resolved) catch error.InternalError;
}

fn bindModule(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.module;
    _ = try tryBindNodes(parser, n.statements);
    return type_mod.the().void_type;
}

fn bindProgram(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.program;
    _ = try tryBindNodes(parser, n.statements);
    return type_mod.the().void_type;
}

fn bindExtern(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.@"extern";
    _ = try tryBindNodes(parser, n.declarations);
    return type_mod.the().void_type;
}

fn bindArgumentList(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.argument_list;
    const types = try tryBindNodes(parser, n.arguments);
    return type_mod.the().typeListOf(types) catch error.InternalError;
}

fn bindCall(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.call;

    // If function is already resolved, use its return type
    if (!n.function.isNull()) {
        _ = try tryBind(parser, n.function);
        const func_type = n.function.getConst().bound_type;
        if (!func_type.isNull() and func_type.getConst().description == .function_type) {
            return func_type.getConst().description.function_type.result;
        }
    }

    // Get argument types
    _ = try tryBind(parser, n.arguments);
    const arg_type = n.arguments.getConst().bound_type;
    if (arg_type.isNull()) return error.Undetermined;

    // Extract function names from callable
    var names = ArrayList([]const u8).init(parser.arena.allocator());
    if (!try extractCallableName(n.callable, &names)) {
        return bindError(parser, node, "Callable must be a function name", .{});
    }

    // Find overloads
    var overloads = ArrayList(AstNode).init(parser.arena.allocator());
    try parser.findOverloads(names.items, &overloads);

    const arg_types: []const TypeHandle = if (arg_type.getConst().description == .type_list)
        arg_type.getConst().description.type_list.types
    else
        &[_]TypeHandle{arg_type};

    // Match non-generic overloads
    for (overloads.items) |func_def| {
        _ = try tryBind(parser, func_def);
        const fd_type = func_def.getConst().bound_type;
        if (fd_type.isNull()) continue;
        if (fd_type.getConst().description != .function_type) continue;
        const ft = fd_type.getConst().description.function_type;
        if (ft.parameters.isNull()) continue;
        const param_types = ft.parameters.getConst().description.type_list.types;
        if (param_types.len != arg_types.len) continue;
        var matches = true;
        for (param_types, arg_types) |pt, at| {
            const pv = pt.getConst().value_type();
            const av = at.getConst().value_type();
            if (!TypeHandle.eql(pv, av)) { matches = false; break; }
        }
        if (matches) {
            node.get().node.call.function = func_def;
            return ft.result;
        }
    }

    if (parser.pass == 0) return error.Undetermined;
    const name = try std.mem.join(parser.arena.allocator(), ".", names.items);
    return bindError(parser, node, "Unresolved function '{s}'", .{name});
}

fn extractCallableName(node: AstNode, names: *ArrayList([]const u8)) !bool {
    return switch (node.getConst().node) {
        .identifier => |id| { try names.append(id.identifier); return true; },
        .stamped_identifier => |id| { try names.append(id.identifier); return true; },
        .identifier_list => |list| {
            for (list.identifiers) |id| try names.append(id);
            return true;
        },
        else => false,
    };
}

fn bindFunctionDeclaration(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.function_declaration;
    const param_types = try tryBindNodes(parser, n.parameters);
    _ = try tryBind(parser, n.return_type);
    const ret_bound = n.return_type.getConst().bound_type;
    const ret = if (!ret_bound.isNull() and ret_bound.getConst().description == .type_type)
        ret_bound.getConst().description.type_type.type
    else
        type_mod.the().void_type;
    return type_mod.the().functionOf(param_types, ret) catch error.InternalError;
}

fn bindFunctionDefinition(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.function_definition;

    // Register generic parameters if any
    if (!n.declaration.isNull()) {
        const decl = &n.declaration.getConst().node.function_declaration;
        for (decl.generics) |gp| {
            const gname = gp.getConst().node.identifier.identifier;
            const gtype = type_mod.the().genericParameter(gname) catch continue;
            try parser.registerType(gname, gtype);
        }
    }

    const func_type = try tryBind(parser, n.declaration);

    // Register function in parent namespace (if not generic)
    const is_generic = !n.declaration.isNull() and
        n.declaration.getConst().node.function_declaration.generics.len > 0;

    if (!is_generic) {
        if (parser.currentNs()) |ns| {
            const parent = ns.getConst().parent;
            if (!parent.isNull()) {
                try parent.get().registerFunction(n.name, node);
            } else {
                try ns.get().registerFunction(n.name, node);
            }
        }
    }

    if (!is_generic) {
        // Bind parameters into the function's namespace
        const decl = &n.declaration.getConst().node.function_declaration;
        for (decl.parameters) |param| {
            const pname = param.getConst().node.parameter.name;
            if (!parser.hasVariable(&[_][]const u8{pname})) {
                try parser.registerVariable(pname, param);
            }
        }
        _ = try tryBind(parser, n.implementation);
    }

    return func_type;
}

fn bindParameter(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.parameter;
    _ = try tryBind(parser, n.type_name);
    const bt = n.type_name.getConst().bound_type;
    if (bt.isNull()) return error.Undetermined;
    return if (bt.getConst().description == .type_type)
        bt.getConst().description.type_type.type
    else
        bt;
}

fn bindBlock(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.block;
    const types = try tryBindNodes(parser, n.statements);
    if (types.len == 0) return type_mod.the().void_type;
    return types[types.len - 1];
}

fn bindReturn(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.@"return";
    if (!n.expression.isNull()) _ = try tryBind(parser, n.expression);

    const func = parser.currentFunction();
    if (func.isNull()) {
        return bindError(parser, node, "`return` outside function", .{});
    }
    const func_sig = func.getConst().bound_type;
    if (func_sig.isNull() or func_sig.getConst().description != .function_type) {
        return error.Undetermined;
    }
    const ret_type = func_sig.getConst().description.function_type.result;

    if (n.expression.isNull()) {
        if (!TypeHandle.eql(ret_type, type_mod.the().void_type)) {
            return bindError(parser, node,
                "`return` requires an expression of type '{s}'",
                .{ret_type.getConst().name});
        }
        return ret_type;
    }
    const expr_type = n.expression.getConst().bound_type;
    if (!expr_type.isNull()) {
        if (!expr_type.getConst().assignable_to(ret_type) and
            !TypeHandle.eql(expr_type, ret_type))
        {
            return bindError(parser, node,
                "`return` expression type '{s}' not assignable to '{s}'",
                .{ expr_type.getConst().name, ret_type.getConst().name });
        }
    }
    return ret_type;
}

fn bindDeferStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.defer_statement;
    _ = try tryBind(parser, n.statement);
    return type_mod.the().void_type;
}

fn bindForStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.for_statement;
    _ = try tryBind(parser, n.range_expr);
    const range_type = n.range_expr.getConst().bound_type;
    if (range_type.isNull()) return error.Undetermined;

    // Determine the variable type from the range
    const var_type: TypeHandle = switch (range_type.getConst().description) {
        .range_type => |r| r.range_of,
        .type_type => |tt| blk: {
            // Enum type used as for range
            if (tt.type.getConst().description == .enum_type) {
                break :blk tt.type;
            }
            return bindError(parser, node, "`for` range type must be a range or enum", .{});
        },
        else => return bindError(parser, node,
            "`for` range expression is not a range: '{s}'", .{range_type.getConst().name}),
    };

    // Register the range variable in the for's namespace
    if (!node.getConst().ns.isNull()) {
        try node.getConst().ns.get().registerVariable(n.range_variable,
            try parser.makeNode(node.getConst().location, .{ .identifier = .{ .identifier = n.range_variable } }));
        // Set the variable's type
        const var_node = node.getConst().ns.getConst().findVariable(n.range_variable) orelse return error.InternalError;
        var_node.get().bound_type = var_type;
        var_node.get().status = .bound;
    }

    return try tryBind(parser, n.statement);
}

fn bindIfStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.if_statement;
    _ = try tryBind(parser, n.condition);
    const cond_type = n.condition.getConst().bound_type;
    if (!cond_type.isNull() and !cond_type.getConst().assignable_to(type_mod.the().boolean)) {
        return bindError(parser, node,
            "`if` condition is '{s}', not boolean", .{cond_type.getConst().name});
    }
    _ = try tryBind(parser, n.if_branch);
    if (!n.else_branch.isNull()) _ = try tryBind(parser, n.else_branch);
    const if_type = n.if_branch.getConst().bound_type;
    if (n.else_branch.isNull() or n.else_branch.getConst().bound_type.isNull()) return if_type;
    if (TypeHandle.eql(if_type, n.else_branch.getConst().bound_type)) return if_type;
    return error.Ambiguous;
}

fn bindLoopStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.loop_statement;
    return tryBind(parser, n.statement);
}

fn bindWhileStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.while_statement;
    _ = try tryBind(parser, n.condition);
    const cond_type = n.condition.getConst().bound_type;
    if (!cond_type.isNull() and cond_type.getConst().description != .bool_type) {
        return bindError(parser, node,
            "`while` condition is '{s}', not boolean", .{cond_type.getConst().name});
    }
    return tryBind(parser, n.statement);
}

fn bindYield(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.yield;
    return tryBind(parser, n.statement);
}

fn bindSwitchCase(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.switch_case;
    _ = try tryBind(parser, n.case_value);
    _ = try tryBind(parser, n.statement);
    return n.statement.getConst().bound_type;
}

fn bindSwitchStatement(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.switch_statement;
    _ = try tryBind(parser, n.switch_value);
    _ = try tryBindNodes(parser, n.switch_cases);

    const switch_type = n.switch_value.getConst().bound_type;
    if (switch_type.isNull()) return error.Undetermined;

    // Exhaustiveness check
    const cardi = cardinality(switch_type);
    var case_count: usize = 0;
    var has_default = false;
    for (n.switch_cases) |c| {
        const cv = c.getConst().node.switch_case.case_value;
        switch (cv.getConst().node) {
            .default_switch_value => { has_default = true; case_count += 1; },
            .expression_list => |list| {
                for (list.expressions) |e| {
                    if (e.getConst().node == .default_switch_value) has_default = true;
                    case_count += 1;
                }
            },
            else => case_count += 1,
        }
    }
    if (has_default) case_count -= 1;

    if (cardi) |total| {
        if (case_count < total and !has_default) {
            return bindError(parser, node,
                "Not all values of type '{s}' are handled in switch",
                .{switch_type.getConst().name});
        }
        if (case_count > total) {
            return bindError(parser, node,
                "Duplicate case in switch over '{s}'", .{switch_type.getConst().name});
        }
    } else {
        if (!has_default) {
            return bindError(parser, node,
                "Switch over '{s}' must have a default case", .{switch_type.getConst().name});
        }
    }

    if (n.switch_cases.len == 0) return type_mod.the().void_type;
    return n.switch_cases[0].getConst().bound_type;
}

fn bindExpressionList(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.expression_list;
    const types = try tryBindNodes(parser, n.expressions);
    return type_mod.the().typeListOf(types) catch error.InternalError;
}

fn bindBinaryExpression(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.binary_expression;

    if (n.op == .member_access) {
        return bindMemberAccess(parser, node);
    }

    const lhs_type = try tryBind(parser, n.lhs);
    const lhs_value_type = lhs_type.getConst().value_type();
    const rhs_type = try tryBind(parser, n.rhs);
    const rhs_value_type = rhs_type.getConst().value_type();

    if (n.op == .assign) {
        // Check compatibility
        if (!lhs_value_type.getConst().compatible(rhs_value_type) and
            !rhs_value_type.getConst().assignable_to(lhs_value_type))
        {
            return bindError(parser, node,
                "Cannot assign '{s}' to '{s}'",
                .{ rhs_type.getConst().name, lhs_type.getConst().name });
        }
        return lhs_type;
    }

    if (n.op == .range) {
        if (TypeHandle.eql(lhs_value_type, rhs_value_type)) {
            if (lhs_value_type.getConst().description == .int_type or
                lhs_value_type.getConst().description == .enum_type)
            {
                return type_mod.the().rangeOf(lhs_value_type) catch error.InternalError;
            }
        }
        return bindError(parser, node, "Range requires matching integer or enum types", .{});
    }

    if (n.op == .cast) {
        // RHS should be a TypeType
        if (rhs_value_type.getConst().description != .type_type) {
            return bindError(parser, node, "`cast` requires a type as right-hand side", .{});
        }
        return rhs_value_type.getConst().description.type_type.type;
    }

    // Check binary operator table
    if (lookupBinaryOp(n.op, lhs_value_type, rhs_value_type)) |result| {
        return result;
    }

    // Comparison operators
    switch (n.op) {
        .equals, .not_equal, .less, .less_equal, .greater, .greater_equal => {
            return type_mod.the().boolean;
        },
        .logical_and, .logical_or => return type_mod.the().boolean,
        else => {},
    }

    if (parser.pass == 0) return error.Undetermined;
    return bindError(parser, node,
        "Operator cannot be applied to '{s}' and '{s}'",
        .{ lhs_value_type.getConst().name, rhs_value_type.getConst().name });
}

fn bindMemberAccess(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.binary_expression;
    const lhs_type = try tryBind(parser, n.lhs);
    const lhs_value_type = lhs_type.getConst().value_type();

    // Type.Member → enum/tagged union constructor
    if (lhs_value_type.getConst().description == .type_type) {
        const inner_type = lhs_value_type.getConst().description.type_type.type;
        const rhs_id = n.rhs.getConst().node.identifier.identifier;
        switch (inner_type.getConst().description) {
            .enum_type => |et| {
                if (et.valueFor(rhs_id)) |val| {
                    const tag = try parser.makeNode(node.getConst().location, .{ .tag_value = .{
                        .operand = AstNode.null_handle,
                        .tag_value = val,
                        .label = rhs_id,
                        .payload_type = type_mod.the().void_type,
                        .payload = AstNode.null_handle,
                    } });
                    tag.get().bound_type = inner_type;
                    tag.get().status = .bound;
                    node.get().superceded_by = tag;
                    return inner_type;
                }
                return bindError(parser, node, "Unknown enum value '{s}'", .{rhs_id});
            },
            .tagged_union_type => |tu| {
                if (tu.valueFor(rhs_id)) |val| {
                    const payload_type = tu.payloadFor(val);
                    const tag = try parser.makeNode(node.getConst().location, .{ .tag_value = .{
                        .operand = AstNode.null_handle,
                        .tag_value = val,
                        .label = rhs_id,
                        .payload_type = payload_type,
                        .payload = AstNode.null_handle,
                    } });
                    tag.get().bound_type = inner_type;
                    tag.get().status = .bound;
                    node.get().superceded_by = tag;
                    return inner_type;
                }
                return bindError(parser, node, "Unknown tagged union value '{s}'", .{rhs_id});
            },
            else => return bindError(parser, node,
                "Left-hand side of '.' with type '{s}' is not a struct, enum, or tagged union",
                .{inner_type.getConst().name}),
        }
    }

    // Module access
    if (lhs_value_type.getConst().description == .module_type) {
        const proxy = n.lhs.getConst().node.module_proxy;
        const label = n.rhs.getConst().node.identifier.identifier;
        const mod = proxy.module;
        if (mod.isNull()) return error.Undetermined;
        const ns = mod.getConst().ns;
        if (ns.isNull()) return error.Undetermined;
        const t = ns.getConst().findType(label);
        if (t) |found| return found;
        const v = ns.getConst().findVariable(label);
        if (v) |var_node| return var_node.getConst().bound_type;
        if (parser.pass == 0) return error.Undetermined;
        return bindError(parser, node, "Unknown member '{s}' in module", .{label});
    }

    // Struct field access
    if (lhs_value_type.getConst().description == .struct_type) {
        const s = &lhs_value_type.getConst().description.struct_type;
        const label = n.rhs.getConst().node.identifier.identifier;
        for (s.fields) |f| {
            if (std.mem.eql(u8, f.name, label)) {
                n.rhs.get().bound_type = f.type;
                n.rhs.get().status = .bound;
                return type_mod.the().referencing(f.type) catch error.InternalError;
            }
        }
        return bindError(parser, node,
            "Unknown struct field '{s}' in type '{s}'",
            .{ label, lhs_value_type.getConst().name });
    }

    // Tagged union member assignment
    if (lhs_value_type.getConst().description == .tagged_union_type) {
        return lhs_value_type;
    }

    if (parser.pass == 0) return error.Undetermined;
    return bindError(parser, node,
        "Left-hand side of '.' has type '{s}' which does not support member access",
        .{lhs_value_type.getConst().name});
}

// Look up in the built-in binary operator table.
fn lookupBinaryOp(op: Operator, lhs: TypeHandle, rhs: TypeHandle) ?TypeHandle {
    const reg = type_mod.the();
    const l = lhs.getConst();
    const r = rhs.getConst();
    // Arithmetic: both sides same integer or float type
    switch (op) {
        .add, .subtract, .multiply, .divide, .modulo => {
            if (TypeHandle.eql(lhs, rhs)) {
                if (l.description == .int_type or l.description == .float_type) return lhs;
            }
        },
        .binary_and, .binary_or, .binary_xor, .shift_left, .shift_right => {
            if (TypeHandle.eql(lhs, rhs) and l.description == .int_type) return lhs;
        },
        .logical_and, .logical_or => {
            if (l.description == .bool_type and r.description == .bool_type) return reg.boolean;
        },
        .subscript => {
            // arr[ix]
            switch (l.description) {
                .slice_type => |sl| return sl.slice_of,
                .array => |a| return a.array_of,
                .dyn_array => |da| return da.array_of,
                else => {},
            }
        },
        .length => return reg.i64_type,
        else => {},
    }
    return null;
}

fn bindUnaryExpression(parser: *Parser, node: AstNode) BindResult {
    const n = &node.getConst().node.unary_expression;
    const operand_type = try tryBind(parser, n.operand);
    const ot = operand_type.getConst().value_type();

    return switch (n.op) {
        .sizeof => type_mod.the().i64_type,
        .address_of => type_mod.the().referencing(ot) catch error.InternalError,
        .negate => blk: {
            if (ot.getConst().description == .int_type or
                ot.getConst().description == .float_type) break :blk ot;
            break :blk bindError(parser, node, "Cannot negate type '{s}'", .{ot.getConst().name});
        },
        .logical_invert => type_mod.the().boolean,
        .binary_invert => blk: {
            if (ot.getConst().description == .int_type) break :blk ot;
            break :blk bindError(parser, node, "Cannot invert type '{s}'", .{ot.getConst().name});
        },
        .length => type_mod.the().i64_type,
        .unwrap => blk: {
            switch (ot.getConst().description) {
                .optional_type => |o| break :blk type_mod.the().referencing(o.type) catch error.InternalError,
                .result_type => |r| break :blk type_mod.the().referencing(r.success) catch error.InternalError,
                .tagged_union_type => break :blk ot,
                else => break :blk bindError(parser, node,
                    "Cannot unwrap type '{s}'", .{ot.getConst().name}),
            }
        },
        .unwrap_error => blk: {
            switch (ot.getConst().description) {
                .result_type => |r| break :blk type_mod.the().referencing(r.error_type) catch error.InternalError,
                else => break :blk bindError(parser, node,
                    "Cannot get error from type '{s}'", .{ot.getConst().name}),
            }
        },
        .idempotent => ot,
        else => ot,
    };
}

// ── bindAll: multi-pass outer loop ────────────────────────────────────────────

pub fn bindAll(parser: *Parser) !void {
    const root = parser.program;
    if (root.isNull()) return;

    // Normalize first
    const norm = try normalize(parser, root);
    parser.program = norm;

    const max_passes = 20;
    var pass: i32 = 0;
    while (pass < max_passes) : (pass += 1) {
        parser.pass = pass;
        parser.unbound = 0;
        const result = bindNode(parser, parser.program);
        if (result) |_| {
            if (parser.unbound == 0) return; // done
        } else |err| switch (err) {
            error.Undetermined => {}, // try again next pass
            error.BindErrors => return error.BindErrors,
            error.Ambiguous => return error.Ambiguous,
            error.InternalError, error.OutOfMemory => return error.InternalError,
        }
        if (parser.unbound == 0) return;
    }
    parser.appendError(.{}, "Bind failed after {} passes: circular dependencies", .{max_passes});
    return error.BindErrors;
}
