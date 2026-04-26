// replaces Lang/SyntaxNode.h + Lang/SyntaxNode.cpp
const std = @import("std");
const ArrayList = std.array_list.Managed;
const Handle = @import("../Util/handle.zig").Handle;
const TokenLocation = @import("../Util/token_location.zig").TokenLocation;
const type_mod = @import("type.zig");
const op_mod = @import("operator.zig");

pub const TypeHandle = type_mod.TypeHandle;
pub const Operator = op_mod.Operator;
pub const OperandType = op_mod.OperandType;

pub const QuoteType = @import("../Util/lexer.zig").QuoteType;
pub const Radix = @import("../Util/lexer.zig").Radix;

// ── Bind result types ─────────────────────────────────────────────────────────

pub const AstStatus = enum {
    initialized,
    normalized,
    undetermined,
    bound,
    ambiguous,
    bind_errors,
    internal_error,
};

pub const BindError = error{ Undetermined, Ambiguous, BindErrors, InternalError, OutOfMemory };
pub const BindResult = BindError!TypeHandle;

// ── AST handle types ──────────────────────────────────────────────────────────

// AstNode is a stable index into Parser.nodes that follows the superceded_by
// chain on every get()/getConst() — mirroring C++ Parser::hunt().
// AstNodeImpl is defined later in this file; Zig resolves forward refs at comptime.
pub const AstNode = struct {
    list: ?*ArrayList(AstNodeImpl) = null,
    index: usize = 0,

    pub const null_handle: AstNode = .{ .list = null, .index = 0 };

    pub fn init(list: *ArrayList(AstNodeImpl), index: usize) AstNode {
        return .{ .list = list, .index = index };
    }

    pub fn append(list: *ArrayList(AstNodeImpl), value: AstNodeImpl) !AstNode {
        try list.append(value);
        return .{ .list = list, .index = list.items.len - 1 };
    }

    pub fn isNull(self: AstNode) bool {
        return self.list == null;
    }

    // Follow the superceded_by chain — always return the latest node.
    pub fn get(self: AstNode) *AstNodeImpl {
        const list = self.list.?;
        var idx = self.index;
        while (true) {
            const item = &list.items[idx];
            if (item.superceded_by.list == null) return item;
            idx = item.superceded_by.index;
        }
    }

    pub fn getConst(self: AstNode) *const AstNodeImpl {
        const list = self.list.?;
        var idx = self.index;
        while (true) {
            const item = &list.items[idx];
            if (item.superceded_by.list == null) return item;
            idx = item.superceded_by.index;
        }
    }

    // Return an AstNode handle pointing to the resolved (end-of-chain) node.
    pub fn resolve(self: AstNode) AstNode {
        const list = self.list.?;
        var idx = self.index;
        while (list.items[idx].superceded_by.list != null) {
            idx = list.items[idx].superceded_by.index;
        }
        return .{ .list = list, .index = idx };
    }

    pub fn eql(a: AstNode, b: AstNode) bool {
        if (a.list != b.list) return false;
        if (a.list == null) return true;
        return a.index == b.index;
    }
};
pub const AstNodes = []AstNode;

// ── Utility types ─────────────────────────────────────────────────────────────

pub const Label = ?[]const u8;     // replaces std::optional<std::wstring>
pub const Strings = [][]const u8;  // replaces std::vector<std::wstring>

pub const Visibility = enum { static, public, export_vis };

// ── Syntax node structs ───────────────────────────────────────────────────────
// Each struct carries only data fields.  bind() and normalize() are dispatched
// in lang/ast/*.zig (Session 4); stubs live at the bottom of this file.

pub const Dummy = struct {};

pub const Alias = struct {
    name: []const u8,
    aliased_type: AstNode,
};

pub const ArgumentList = struct {
    arguments: AstNodes,
};

pub const BinaryExpression = struct {
    lhs: AstNode,
    op: Operator,
    rhs: AstNode,
};

pub const Block = struct {
    statements: AstNodes,
    label: Label,
};

pub const BoolConstant = struct {
    value: bool,
};

pub const Break = struct {
    label: Label,
    block: AstNode,
};

pub const Call = struct {
    callable: AstNode,
    arguments: AstNode,
    function: AstNode, // resolved during bind; null_handle before
};

pub const Comptime = struct {
    script_text: []const u8,
    statements: AstNode,
    output: []const u8,
};

pub const Continue = struct {
    label: Label,
};

pub const CString = struct {
    string: []const u8,
};

pub const Decimal = struct {
    value: f64,
};

pub const DefaultSwitchValue = struct {};

pub const DeferStatement = struct {
    statement: AstNode,
};

pub const Embed = struct {
    file_name: []const u8,
};

pub const EnumValue = struct {
    label: []const u8,
    value: AstNode,
    payload: AstNode,
};

pub const Enum = struct {
    name: []const u8,
    underlying_type: AstNode,
    values: AstNodes,
};

pub const ExportDeclaration = struct {
    name: []const u8,
    declaration: AstNode,
};

pub const ExpressionList = struct {
    expressions: AstNodes,
};

pub const Extern = struct {
    declarations: AstNodes,
    library: []const u8,
};

pub const ExternLink = struct {
    link_name: []const u8,
};

pub const ForStatement = struct {
    range_variable: []const u8,
    range_expr: AstNode,
    statement: AstNode,
    label: Label,
};

pub const FunctionDeclaration = struct {
    name: []const u8,
    generics: AstNodes,
    parameters: AstNodes,
    return_type: AstNode,
};

pub const FunctionDefinition = struct {
    name: []const u8,
    declaration: AstNode,
    implementation: AstNode,
    visibility: Visibility = .static,
};

pub const Identifier = struct {
    identifier: []const u8,
};

pub const IdentifierList = struct {
    identifiers: Strings,
};

pub const IfStatement = struct {
    condition: AstNode,
    if_branch: AstNode,
    else_branch: AstNode,
    label: Label,
};

pub const Import = struct {
    file_name: Strings,
};

pub const Include = struct {
    file_name: []const u8,
};

pub const LoopStatement = struct {
    label: Label,
    statement: AstNode,
};

pub const Module = struct {
    name: []const u8,
    source: []const u8,
    statements: AstNodes,
};

pub const ModuleProxy = struct {
    name: []const u8,
    module: AstNode,
};

pub const Nullptr = struct {};

// Numeric literal; stores the most specific integer type determined at parse time.
pub const Number = struct {
    pub const Int = union(enum) {
        u64: u64,
        i64: i64,
        u32: u32,
        i32: i32,
        u16: u16,
        i16: i16,
        u8: u8,
        i8: i8,
    };
    value: Int,
};

pub const Parameter = struct {
    name: []const u8,
    type_name: AstNode,
};

pub const Program = struct {
    name: []const u8,
    source: []const u8,
    statements: AstNodes,
};

pub const PublicDeclaration = struct {
    name: []const u8,
    declaration: AstNode,
};

pub const QuotedString = struct {
    string: []const u8,
    quote_type: QuoteType,
};

pub const Return = struct {
    expression: AstNode,
};

// StampedIdentifier is an Identifier with generic type arguments.
pub const StampedIdentifier = struct {
    identifier: []const u8,
    arguments: AstNodes,
};

pub const String = struct {
    string: []const u8,
};

pub const StructMember = struct {
    label: []const u8,
    member_type: AstNode,
};

pub const Struct = struct {
    name: []const u8,
    members: AstNodes,
};

pub const SwitchCase = struct {
    case_value: AstNode,
    binding: AstNode,
    statement: AstNode,
};

pub const SwitchStatement = struct {
    label: Label,
    switch_value: AstNode,
    switch_cases: AstNodes,
};

// TagValue is a resolved tagged-union constructor, produced during bind.
pub const TagValue = struct {
    operand: AstNode,       // null_handle for literal form
    tag_value: i64,
    label: []const u8,
    payload_type: TypeHandle,
    payload: AstNode,
};

// TypeSpecification describes a type annotation in source code.
pub const TypeSpecification = struct {
    pub const Description = union(enum) {
        type_name: TypeNameNode,
        reference: ReferenceDescriptionNode,
        slice: SliceDescriptionNode,
        zero_terminated_array: ZeroTerminatedArrayDescriptionNode,
        array: ArrayDescriptionNode,
        dyn_array: DynArrayDescriptionNode,
        optional: OptionalDescriptionNode,
        pointer: PointerDescriptionNode,
        result: ResultDescriptionNode,
    };
    description: Description,
};

pub const TypeNameNode = struct {
    name: Strings,
    arguments: AstNodes,
};

pub const ReferenceDescriptionNode = struct { referencing: AstNode };
pub const SliceDescriptionNode = struct { slice_of: AstNode };
pub const ZeroTerminatedArrayDescriptionNode = struct { array_of: AstNode };
pub const ArrayDescriptionNode = struct { array_of: AstNode, size: usize };
pub const DynArrayDescriptionNode = struct { array_of: AstNode };
pub const OptionalDescriptionNode = struct { optional_of: AstNode };
pub const PointerDescriptionNode = struct { referencing: AstNode };
pub const ResultDescriptionNode = struct { success: AstNode, @"error": AstNode };

pub const UnaryExpression = struct {
    op: Operator,
    operand: AstNode,
};

pub const VariableDeclaration = struct {
    name: []const u8,
    type_name: AstNode,
    initializer: AstNode,
    is_const: bool,
    visibility: Visibility = .static,
};

pub const Void = struct {};

pub const WhileStatement = struct {
    label: Label,
    condition: AstNode,
    statement: AstNode,
};

pub const Yield = struct {
    label: Label,
    statement: AstNode,
};

// ── SyntaxNode: the main discriminated union ──────────────────────────────────
// Variant order matches the C++ SyntaxNodeTypes macro for compatibility.

pub const SyntaxNode = union(enum) {
    dummy: Dummy,
    alias: Alias,
    argument_list: ArgumentList,
    binary_expression: BinaryExpression,
    block: Block,
    bool_constant: BoolConstant,
    @"break": Break,
    call: Call,
    comptime_node: Comptime,
    @"continue": Continue,
    cstring: CString,
    decimal: Decimal,
    default_switch_value: DefaultSwitchValue,
    defer_statement: DeferStatement,
    embed: Embed,
    @"enum": Enum,
    enum_value: EnumValue,
    export_declaration: ExportDeclaration,
    expression_list: ExpressionList,
    @"extern": Extern,
    extern_link: ExternLink,
    for_statement: ForStatement,
    function_declaration: FunctionDeclaration,
    function_definition: FunctionDefinition,
    identifier: Identifier,
    identifier_list: IdentifierList,
    if_statement: IfStatement,
    include: Include,
    import: Import,
    loop_statement: LoopStatement,
    module: Module,
    module_proxy: ModuleProxy,
    null_ptr: Nullptr,
    number: Number,
    parameter: Parameter,
    program: Program,
    public_declaration: PublicDeclaration,
    quoted_string: QuotedString,
    @"return": Return,
    stamped_identifier: StampedIdentifier,
    string: String,
    @"struct": Struct,
    struct_member: StructMember,
    switch_case: SwitchCase,
    switch_statement: SwitchStatement,
    tag_value: TagValue,
    type_specification: TypeSpecification,
    unary_expression: UnaryExpression,
    variable_declaration: VariableDeclaration,
    void_node: Void,
    while_statement: WhileStatement,
    yield: Yield,
};

pub const SyntaxNodeKind = std.meta.Tag(SyntaxNode);

// ── Namespace ─────────────────────────────────────────────────────────────────

pub const NsHandle = Handle(Namespace);

pub const NsEntry = union(enum) {
    variable: AstNode,
    function: AstNode,
    module_ref: AstNode,
    type_ref: TypeHandle,
};

pub const NsEntryPair = struct {
    name: []const u8,
    entry: NsEntry,
};

pub const Namespace = struct {
    id: NsHandle = NsHandle.null_handle,
    node: AstNode,
    entries: ArrayList(NsEntryPair),
    parent: NsHandle = NsHandle.null_handle,

    pub fn init(allocator: std.mem.Allocator, node: AstNode) Namespace {
        return .{ .node = node, .entries = ArrayList(NsEntryPair).init(allocator) };
    }

    pub fn deinit(self: *Namespace) void {
        self.entries.deinit();
    }

    pub fn contains(self: *const Namespace, name: []const u8) bool {
        for (self.entries.items) |e| if (std.mem.eql(u8, e.name, name)) return true;
        return false;
    }

    pub fn findVariable(self: *const Namespace, name: []const u8) ?AstNode {
        var i = self.entries.items.len;
        while (i > 0) {
            i -= 1;
            const e = self.entries.items[i];
            if (e.entry == .variable and std.mem.eql(u8, e.name, name)) return e.entry.variable;
        }
        if (!self.parent.isNull()) return self.parent.getConst().findVariable(name);
        return null;
    }

    pub fn findType(self: *const Namespace, name: []const u8) ?TypeHandle {
        for (self.entries.items) |e| {
            if (e.entry == .type_ref and std.mem.eql(u8, e.name, name)) return e.entry.type_ref;
        }
        if (!self.parent.isNull()) return self.parent.getConst().findType(name);
        return null;
    }

    pub fn findFunctions(self: *const Namespace, name: []const u8, out: *ArrayList(AstNode)) !void {
        for (self.entries.items) |e| {
            if (e.entry == .function and std.mem.eql(u8, e.name, name)) try out.append(e.entry.function);
        }
        if (!self.parent.isNull()) try self.parent.getConst().findFunctions(name, out);
    }

    pub fn findModule(self: *const Namespace, name: []const u8) ?AstNode {
        for (self.entries.items) |e| {
            if (e.entry == .module_ref and std.mem.eql(u8, e.name, name)) return e.entry.module_ref;
        }
        if (!self.parent.isNull()) return self.parent.getConst().findModule(name);
        return null;
    }

    pub fn registerVariable(self: *Namespace, name: []const u8, node: AstNode) !void {
        try self.entries.append(.{ .name = name, .entry = .{ .variable = node } });
    }

    pub fn registerFunction(self: *Namespace, name: []const u8, node: AstNode) !void {
        try self.entries.append(.{ .name = name, .entry = .{ .function = node } });
    }

    pub fn registerType(self: *Namespace, name: []const u8, typ: TypeHandle) !void {
        try self.entries.append(.{ .name = name, .entry = .{ .type_ref = typ } });
    }

    pub fn registerModule(self: *Namespace, name: []const u8, node: AstNode) !void {
        try self.entries.append(.{ .name = name, .entry = .{ .module_ref = node } });
    }
};

// ── AstNodeImpl: the heap-allocated node record ───────────────────────────────

pub const NodeTag = union(enum) {
    bool_val: bool,
    int_val: i64,
    str_val: []const u8,
    type_val: TypeHandle,
    node_val: AstNode,
};

pub const AstNodeImpl = struct {
    location: TokenLocation = .{},
    status: AstStatus = .initialized,
    node: SyntaxNode = .{ .dummy = .{} },
    bound_type: TypeHandle = TypeHandle.null_handle,
    ns: NsHandle = NsHandle.null_handle,
    id: AstNode = AstNode.null_handle,
    supercedes: AstNode = AstNode.null_handle,
    superceded_by: AstNode = AstNode.null_handle,
    tag: NodeTag = .{ .bool_val = false },

    pub fn kind(self: *const AstNodeImpl) SyntaxNodeKind {
        return std.meta.activeTag(self.node);
    }
};

// ── Helper: extract identifier text from Identifier or StampedIdentifier ──────

pub fn identifierText(node: AstNode) []const u8 {
    return switch (node.getConst().node) {
        .identifier => |n| n.identifier,
        .stamped_identifier => |n| n.identifier,
        else => unreachable,
    };
}

// ── Helper: extract name from named nodes (Enum, FunctionDefinition, etc.) ───

pub fn nodeName(node: AstNode) []const u8 {
    return switch (node.getConst().node) {
        .@"enum" => |n| n.name,
        .function_definition => |n| n.name,
        .function_declaration => |n| n.name,
        .module => |n| n.name,
        .program => |n| n.name,
        else => unreachable,
    };
}

// ── Operator tables ───────────────────────────────────────────────────────────

pub const Operand = struct {
    type: OperandType,
};

pub const BinaryOperator = struct {
    lhs: Operand,
    op: Operator,
    rhs: Operand,
    result: OperandType,
};

pub const UnaryOperator = struct {
    op: Operator,
    operand: Operand,
    result: OperandType,
};
