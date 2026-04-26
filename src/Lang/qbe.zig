// QBE IL backend for the cathode compiler.
// Translates a typed AST (after bind()) into QBE intermediate language,
// then invokes the qbe tool + assembler + linker to produce a native binary.
const std = @import("std");
const ArrayList = std.array_list.Managed;
const Allocator = std.mem.Allocator;

const type_mod = @import("type.zig");
const TypeHandle = type_mod.TypeHandle;
const Type = type_mod.Type;
const TypeDescription = type_mod.TypeDescription;

const sn = @import("syntax_node.zig");
const AstNode = sn.AstNode;
const AstNodeImpl = sn.AstNodeImpl;
const SyntaxNode = sn.SyntaxNode;

const parser_mod = @import("parser.zig");
const Parser = parser_mod.Parser;

const op_mod = @import("operator.zig");
const Operator = op_mod.Operator;

fn alignAt(bytes: usize, alignment: usize) usize {
    if (alignment == 0) return bytes;
    return (bytes + alignment - 1) & ~(alignment - 1);
}

// ── ILBaseType ────────────────────────────────────────────────────────────────

pub const ILBaseType = enum(u8) {
    V  = 0x00,
    B  = 0x04,
    SB = 0x05,
    UB = 0x06,
    H  = 0x08,
    SH = 0x09,
    UH = 0x0A,
    W  = 0x10,
    SW = 0x11,
    UW = 0x12,
    L  = 0x20,
    S  = 0x40,
    D  = 0x80,

    pub fn str(self: ILBaseType) []const u8 {
        return switch (self) {
            .V  => "v",  .B  => "b",  .SB => "sb", .UB => "ub",
            .H  => "h",  .SH => "sh", .UH => "uh",
            .W  => "w",  .SW => "sw", .UW => "uw",
            .L  => "l",  .S  => "s",  .D  => "d",
        };
    }

    // Strip sign bits → canonical base class (B, H, W, L, S, D, V).
    pub fn basetype(self: ILBaseType) ILBaseType {
        return @enumFromInt(@intFromEnum(self) & 0xFC);
    }

    // QBE target type: only L/S/D/W survive; smaller → W.
    pub fn targettype(self: ILBaseType) ILBaseType {
        return switch (self) {
            .L, .S, .D, .W => self,
            else => .W,
        };
    }

    // Extension width for sub-word values passed in calls.
    pub fn mustExtend(self: ILBaseType) ILBaseType {
        return switch (self) {
            .B, .SB       => .SB,
            .UB           => .UB,
            .H, .SH       => .SH,
            .UH           => .UH,
            .W, .SW, .UW  => .W,
            .L            => .L,
            else          => self,
        };
    }

    pub fn isFloat(self: ILBaseType) bool {
        return self == .S or self == .D;
    }
};

// ── ILAggregate / ILType ──────────────────────────────────────────────────────

pub const ILAggregate = struct {
    name: []const u8,
    size: usize,
    align_bytes: usize,
};

pub const ILType = union(enum) {
    base: ILBaseType,
    aggregate: ILAggregate,

    pub fn str(self: ILType) []const u8 {
        return switch (self) {
            .base      => |b| b.str(),
            .aggregate => |a| a.name,
        };
    }

    pub fn asBase(self: ILType) ILBaseType {
        return switch (self) {
            .base      => |b| b,
            .aggregate => .L,
        };
    }

    pub fn targettype(self: ILType) ILType {
        return switch (self) {
            .base      => |b| .{ .base = b.targettype() },
            .aggregate => .{ .base = .L },
        };
    }

    pub fn isAggregate(self: ILType) bool {
        return self == .aggregate;
    }
};

// ── Type mapping helpers ───────────────────────────────────────────────────────

pub fn qbeFirstClass(th: TypeHandle) bool {
    return switch (th.getConst().description) {
        .int_type, .float_type, .bool_type, .enum_type,
        .pointer_type, .zero_terminated_array, .reference_type => true,
        else => false,
    };
}

pub fn qbeTypeCode(th: TypeHandle) ILType {
    const t = th.getConst();
    return switch (t.description) {
        .int_type => |it| .{ .base = switch (it.width_bits) {
            8  => .B,
            16 => .H,
            32 => .W,
            else => .L,
        }},
        .float_type  => |ft| .{ .base = if (ft.width_bits == 32) .S else .D },
        .bool_type   => .{ .base = .B },
        .enum_type   => |et| qbeTypeCode(et.underlying_type),
        .type_alias  => |ta| qbeTypeCode(ta.alias_of),
        .pointer_type, .zero_terminated_array, .reference_type,
        .function_type, .slice_type, .dyn_array => .{ .base = .L },
        else => .{ .base = .L },
    };
}

pub fn qbeLoadCode(th: TypeHandle) ILBaseType {
    const t = th.getConst();
    return switch (t.description) {
        .int_type => |it| switch (it.width_bits) {
            8  => if (it.is_signed) .SB else .UB,
            16 => if (it.is_signed) .SH else .UH,
            32 => if (it.is_signed) .SW else .UW,
            else => .L,
        },
        .float_type => |ft| if (ft.width_bits == 32) .S else .D,
        .bool_type  => .UB,
        .enum_type  => |et| qbeLoadCode(et.underlying_type),
        .type_alias => |ta| qbeLoadCode(ta.alias_of),
        .pointer_type, .zero_terminated_array, .reference_type,
        .function_type, .slice_type, .dyn_array => .L,
        else => .L,
    };
}

// Returns the QBE IL type for compound/aggregate types.
// Registers the type with the current file if it needs a type definition.
pub fn qbeType(th: TypeHandle, ctx: *QBEContext) !ILType {
    const t = th.getConst();
    return switch (t.description) {
        .slice_type => blk: {
            try ctx.registerAggregateType(th);
            break :blk .{ .aggregate = .{ .name = ":slice_t", .size = 16, .align_bytes = 8 } };
        },
        .optional_type => blk: {
            try ctx.registerAggregateType(th);
            const name = try std.fmt.allocPrint(ctx.arena.allocator(), ":opt{d}", .{th.index});
            break :blk .{ .aggregate = .{ .name = name, .size = t.size_of(), .align_bytes = t.align_of() } };
        },
        .struct_type => blk: {
            try ctx.registerAggregateType(th);
            const name = try std.fmt.allocPrint(ctx.arena.allocator(), ":struct{d}", .{th.index});
            break :blk .{ .aggregate = .{ .name = name, .size = t.size_of(), .align_bytes = t.align_of() } };
        },
        .result_type => blk: {
            try ctx.registerAggregateType(th);
            const name = try std.fmt.allocPrint(ctx.arena.allocator(), ":res{d}", .{th.index});
            break :blk .{ .aggregate = .{ .name = name, .size = t.size_of(), .align_bytes = t.align_of() } };
        },
        .tagged_union_type => blk: {
            try ctx.registerAggregateType(th);
            const name = try std.fmt.allocPrint(ctx.arena.allocator(), ":union{d}", .{th.index});
            break :blk .{ .aggregate = .{ .name = name, .size = t.size_of(), .align_bytes = t.align_of() } };
        },
        else => qbeTypeCode(th),
    };
}

pub fn typeRef(th: TypeHandle, alloc: Allocator) ![]const u8 {
    const t = th.getConst();
    return switch (t.description) {
        .slice_type     => ":slice_t",
        .optional_type  => std.fmt.allocPrint(alloc, ":opt{d}",    .{th.index}),
        .struct_type    => std.fmt.allocPrint(alloc, ":struct{d}", .{th.index}),
        .result_type    => std.fmt.allocPrint(alloc, ":res{d}",    .{th.index}),
        .tagged_union_type => std.fmt.allocPrint(alloc, ":union{d}", .{th.index}),
        else => qbeTypeCode(th).str(),
    };
}

// ── ILOperation ───────────────────────────────────────────────────────────────
// Non-comparison ops MUST precede cmp_eq; ordered comparisons MUST be last.
// This ordering makes isComparison() and isOrderedComparison() work via intFromEnum.

pub const ILOperation = enum {
    add, sub, mul,
    div, udiv,
    rem, urem,
    bit_and, bit_or, bit_xor,
    shl, sar, shr,
    // comparisons — everything from here on
    cmp_eq, cmp_ne,
    // ordered comparisons — everything from here on
    cmp_sge, cmp_sgt, cmp_sle, cmp_slt,
    cmp_uge, cmp_ugt, cmp_ule, cmp_ult,
    cmp_fge, cmp_fgt, cmp_fle, cmp_flt,

    pub fn isComparison(self: ILOperation) bool {
        return @intFromEnum(self) >= @intFromEnum(ILOperation.cmp_eq);
    }
    pub fn isOrderedComparison(self: ILOperation) bool {
        return @intFromEnum(self) >= @intFromEnum(ILOperation.cmp_sge);
    }

    pub fn str(self: ILOperation) []const u8 {
        return switch (self) {
            .add     => "add",  .sub    => "sub",  .mul    => "mul",
            .div     => "div",  .udiv   => "udiv",
            .rem     => "rem",  .urem   => "urem",
            .bit_and => "and",  .bit_or => "or",   .bit_xor => "xor",
            .shl     => "shl",  .sar    => "sar",  .shr    => "shr",
            .cmp_eq  => "ceq",  .cmp_ne => "cne",
            .cmp_sge => "csge", .cmp_sgt => "csgt", .cmp_sle => "csle", .cmp_slt => "cslt",
            .cmp_uge => "cuge", .cmp_ugt => "cugt", .cmp_ule => "cule", .cmp_ult => "cult",
            .cmp_fge => "cge",  .cmp_fgt => "cgt",  .cmp_fle => "cle",  .cmp_flt => "clt",
        };
    }
};

// ── ILValue ───────────────────────────────────────────────────────────────────

pub const ILValue = struct {
    inner: Inner,
    il_type: ILType,

    pub const Inner = union(enum) {
        none,
        local: usize,
        global: []const u8,
        temporary: usize,
        variable: usize,
        parameter: usize,
        return_value,
        int_val: i64,
        float_val: f64,
        literal: []const u8,
        sequence: []const ILValue,
    };

    pub fn makeNone() ILValue {
        return .{ .inner = .none, .il_type = .{ .base = .V } };
    }
    pub fn makeLocal(n: usize, t: ILType) ILValue {
        return .{ .inner = .{ .local = n }, .il_type = t };
    }
    pub fn makeGlobal(name: []const u8, t: ILType) ILValue {
        return .{ .inner = .{ .global = name }, .il_type = t };
    }
    pub fn makeTemporary(n: usize, t: ILType) ILValue {
        return .{ .inner = .{ .temporary = n }, .il_type = t };
    }
    pub fn makeVariable(n: usize, t: ILType) ILValue {
        return .{ .inner = .{ .variable = n }, .il_type = t };
    }
    pub fn makeParameter(n: usize, t: ILType) ILValue {
        return .{ .inner = .{ .parameter = n }, .il_type = t };
    }
    pub fn makeReturnValue(t: ILType) ILValue {
        return .{ .inner = .return_value, .il_type = t };
    }
    pub fn makeInteger(v: i64, t: ILType) ILValue {
        return .{ .inner = .{ .int_val = v }, .il_type = t };
    }
    pub fn makeFloat(v: f64, t: ILType) ILValue {
        return .{ .inner = .{ .float_val = v }, .il_type = t };
    }
    pub fn makeLiteral(s: []const u8, t: ILType) ILValue {
        return .{ .inner = .{ .literal = s }, .il_type = t };
    }
    pub fn makeSequence(vals: []const ILValue, t: ILType) ILValue {
        return .{ .inner = .{ .sequence = vals }, .il_type = t };
    }

    pub fn isNone(self: ILValue) bool {
        return self.inner == .none;
    }

    pub fn isAddress(self: ILValue) bool {
        return switch (self.inner) {
            .variable, .parameter, .return_value, .local, .global => true,
            else => false,
        };
    }

    pub fn write(self: ILValue, writer: anytype) !void {
        switch (self.inner) {
            .none         => {},
            .local      => |n|    try writer.print("%local_{d}", .{n}),
            .global     => |name| try writer.print("${s}", .{name}),
            .temporary  => |n|    try writer.print("%temp_{d}", .{n}),
            .variable   => |n|    try writer.print("%var_{d}", .{n}),
            .parameter  => |n|    try writer.print("%param_{d}", .{n}),
            .return_value =>      try writer.writeAll("%ret"),
            .int_val    => |v|    try writer.print("{d}", .{v}),
            .float_val  => |v| {
                if (self.il_type == .base and self.il_type.base == .S) {
                    const bits: u32 = @bitCast(@as(f32, @floatCast(v)));
                    try writer.print("s_{d}", .{bits});
                } else {
                    const bits: u64 = @bitCast(v);
                    try writer.print("d_{d}", .{bits});
                }
            },
            .literal    => |s| try writer.writeAll(s),
            .sequence   => |vals| {
                for (vals, 0..) |val, i| {
                    if (i > 0) try writer.writeAll(", ");
                    try val.write(writer);
                }
            },
        }
    }
};

// ── Instruction structs ───────────────────────────────────────────────────────

pub const AllocDef = struct {
    target: ILValue,
    align_bytes: u32,
    size_bytes: u64,

    pub fn write(self: AllocDef, writer: anytype) !void {
        try writer.writeAll("    ");
        try self.target.write(writer);
        try writer.print(" =l alloc{d} {d}\n", .{ self.align_bytes, self.size_bytes });
    }
};

pub const BlitDef = struct {
    src: ILValue,
    dest: ILValue,
    bytes: u64,

    pub fn write(self: BlitDef, writer: anytype) !void {
        try writer.writeAll("    blit ");
        try self.src.write(writer);
        try writer.writeAll(", ");
        try self.dest.write(writer);
        try writer.print(", {d}\n", .{self.bytes});
    }
};

pub const CallArg = struct {
    il_type: ILType,
    value: ILValue,
};

pub const CallDef = struct {
    target: ILValue,
    ret_type: ILType,
    func_name: []const u8,
    full_name: []const u8,
    args: []const CallArg,

    pub fn write(self: CallDef, writer: anytype) !void {
        try writer.writeAll("    ");
        if (!self.target.isNone()) {
            try self.target.write(writer);
            try writer.writeAll(" =");
            try writer.writeAll(self.ret_type.str());
            try writer.writeAll(" ");
        }
        try writer.print("call ${s}(", .{self.func_name});
        for (self.args, 0..) |arg, i| {
            if (i > 0) try writer.writeAll(", ");
            try writer.writeAll(arg.il_type.str());
            try writer.writeAll(" ");
            try arg.value.write(writer);
        }
        try writer.writeAll(")");
        if (self.full_name.len > 0 and !std.mem.eql(u8, self.full_name, self.func_name)) {
            try writer.print(" # {s}", .{self.full_name});
        }
        try writer.writeAll("\n");
    }
};

pub const CopyDef = struct {
    target: ILValue,
    source: ILValue,

    pub fn write(self: CopyDef, writer: anytype) !void {
        const tt = self.target.il_type.targettype();
        try writer.writeAll("    ");
        try self.target.write(writer);
        try writer.writeAll(" =");
        try writer.writeAll(tt.str());
        try writer.writeAll(" copy ");
        try self.source.write(writer);
        try writer.writeAll("\n");
    }
};

pub const ExprDef = struct {
    target: ILValue,
    lhs: ILValue,
    rhs: ILValue,
    op: ILOperation,

    pub fn write(self: ExprDef, writer: anytype) !void {
        const tt = self.target.il_type.targettype();
        try writer.writeAll("    ");
        try self.target.write(writer);
        try writer.writeAll(" =");
        try writer.writeAll(tt.str());
        try writer.writeAll(" ");
        if (self.op.isComparison()) {
            const bt = self.lhs.il_type.asBase().basetype();
            try writer.print("{s}{s}", .{ self.op.str(), bt.str() });
        } else {
            try writer.writeAll(self.op.str());
        }
        try writer.writeAll(" ");
        try self.lhs.write(writer);
        try writer.writeAll(", ");
        try self.rhs.write(writer);
        try writer.writeAll("\n");
    }
};

pub const ExtDef = struct {
    target: ILValue,
    source: ILValue,
    ext_type: ILBaseType,

    pub fn write(self: ExtDef, writer: anytype) !void {
        const tt = self.target.il_type.targettype();
        try writer.writeAll("    ");
        try self.target.write(writer);
        try writer.writeAll(" =");
        try writer.writeAll(tt.str());
        try writer.print(" ext{s} ", .{self.ext_type.str()});
        try self.source.write(writer);
        try writer.writeAll("\n");
    }
};

pub const JmpDef = struct {
    label: []const u8,
    pub fn write(self: JmpDef, writer: anytype) !void {
        try writer.print("    jmp @{s}\n", .{self.label});
    }
};

pub const JnzDef = struct {
    condition: ILValue,
    on_true: []const u8,
    on_false: []const u8,
    pub fn write(self: JnzDef, writer: anytype) !void {
        try writer.writeAll("    jnz ");
        try self.condition.write(writer);
        try writer.print(", @{s}, @{s}\n", .{ self.on_true, self.on_false });
    }
};

pub const LabelDef = struct {
    name: []const u8,
    pub fn write(self: LabelDef, writer: anytype) !void {
        try writer.print("@{s}\n", .{self.name});
    }
};

pub const LoadDef = struct {
    target: ILValue,
    pointer: ILValue,
    load_type: ILBaseType,
    pub fn write(self: LoadDef, writer: anytype) !void {
        const tt = self.target.il_type.targettype();
        try writer.writeAll("    ");
        try self.target.write(writer);
        try writer.writeAll(" =");
        try writer.writeAll(tt.str());
        try writer.print(" load{s} ", .{self.load_type.str()});
        try self.pointer.write(writer);
        try writer.writeAll("\n");
    }
};

pub const RetDef = struct {
    value: ILValue,
    pub fn write(self: RetDef, writer: anytype) !void {
        if (self.value.isNone()) {
            try writer.writeAll("    ret\n");
        } else {
            try writer.writeAll("    ret ");
            try self.value.write(writer);
            try writer.writeAll("\n");
        }
    }
};

pub const StoreDef = struct {
    value: ILValue,
    pointer: ILValue,
    pub fn write(self: StoreDef, writer: anytype) !void {
        const bt = self.value.il_type.asBase().basetype();
        try writer.print("    store{s} ", .{bt.str()});
        try self.value.write(writer);
        try writer.writeAll(", ");
        try self.pointer.write(writer);
        try writer.writeAll("\n");
    }
};

pub const DbgFile = struct {
    filename: []const u8,
    pub fn write(self: DbgFile, writer: anytype) !void {
        try writer.print("dbgfile \"{s}\"\n", .{self.filename});
    }
};

pub const ILInstruction = union(enum) {
    alloc:    AllocDef,
    blit:     BlitDef,
    call:     CallDef,
    copy:     CopyDef,
    dbg_file: DbgFile,
    expr:     ExprDef,
    ext:      ExtDef,
    jmp:      JmpDef,
    jnz:      JnzDef,
    label:    LabelDef,
    load:     LoadDef,
    ret:      RetDef,
    store:    StoreDef,

    pub fn write(self: ILInstruction, writer: anytype) !void {
        switch (self) {
            inline else => |inst| try inst.write(writer),
        }
    }
    pub fn isRet(self: ILInstruction) bool   { return self == .ret; }
    pub fn isLabel(self: ILInstruction) bool { return self == .label; }
};

// ── ILBinding / ILTemporary / ILParameter ────────────────────────────────────

pub const ILBinding = struct {
    name: []const u8,
    value: ILValue,
    type_handle: TypeHandle,
};

pub const ILTemporary = struct {
    index: usize,
    type_handle: TypeHandle,
    il_type: ILType,
};

pub const ILParameter = struct {
    name: []const u8,
    type_handle: TypeHandle,
    il_type: ILType,
    var_index: ?usize = null,
};

// ── ILFunction ────────────────────────────────────────────────────────────────

pub const ILFunction = struct {
    name: []const u8,
    full_name: []const u8,
    return_type: TypeHandle,
    is_export: bool,
    parameters: ArrayList(ILParameter),
    bindings: ArrayList(ILBinding),
    temporaries: ArrayList(ILTemporary),
    instructions: ArrayList(ILInstruction),
    after_ret: bool = false,

    pub fn init(
        alloc: Allocator,
        name: []const u8,
        full_name: []const u8,
        return_type: TypeHandle,
        is_export: bool,
    ) ILFunction {
        return .{
            .name = name,
            .full_name = full_name,
            .return_type = return_type,
            .is_export = is_export,
            .parameters  = ArrayList(ILParameter).init(alloc),
            .bindings    = ArrayList(ILBinding).init(alloc),
            .temporaries = ArrayList(ILTemporary).init(alloc),
            .instructions = ArrayList(ILInstruction).init(alloc),
        };
    }

    pub fn deinit(self: *ILFunction) void {
        self.parameters.deinit();
        self.bindings.deinit();
        self.temporaries.deinit();
        self.instructions.deinit();
    }

    // Add a named stack-allocated local variable.
    pub fn addBinding(self: *ILFunction, name: []const u8, th: TypeHandle) !ILValue {
        const idx = self.bindings.items.len;
        const il  = qbeTypeCode(th);
        try self.bindings.append(.{ .name = name, .value = ILValue.makeVariable(idx, il), .type_handle = th });
        return ILValue.makeVariable(idx, il);
    }

    // Add a function parameter; if not a reference, also allocates a stack var.
    pub fn addParameter(self: *ILFunction, name: []const u8, th: TypeHandle) !ILValue {
        const pidx = self.parameters.items.len;
        const il   = qbeTypeCode(th);
        var var_index: ?usize = null;
        if (th.getConst().description != .reference_type) {
            var_index = self.bindings.items.len;
            try self.bindings.append(.{ .name = name, .value = ILValue.makeVariable(self.bindings.items.len, il), .type_handle = th });
        }
        try self.parameters.append(.{ .name = name, .type_handle = th, .il_type = il, .var_index = var_index });
        return ILValue.makeParameter(pidx, il);
    }

    pub fn addTemporary(self: *ILFunction, th: TypeHandle, il: ILType) !ILValue {
        const idx = self.temporaries.items.len;
        try self.temporaries.append(.{ .index = idx, .type_handle = th, .il_type = il });
        return ILValue.makeTemporary(idx, il);
    }

    pub fn findBinding(self: *const ILFunction, name: []const u8) ?ILValue {
        var i = self.bindings.items.len;
        while (i > 0) {
            i -= 1;
            const b = self.bindings.items[i];
            if (std.mem.eql(u8, b.name, name)) return b.value;
        }
        return null;
    }

    pub fn findParameter(self: *const ILFunction, name: []const u8) ?struct { val: ILValue, th: TypeHandle } {
        for (self.parameters.items, 0..) |p, i| {
            if (std.mem.eql(u8, p.name, name))
                return .{ .val = ILValue.makeParameter(i, p.il_type), .th = p.type_handle };
        }
        return null;
    }

    pub fn write(self: *const ILFunction, writer: anytype) !void {
        const rt = self.return_type.getConst();
        if (self.is_export) try writer.writeAll("export ");
        try writer.writeAll("function");
        if (!rt.is_a(.void_type)) {
            try writer.print(" {s}", .{qbeTypeCode(self.return_type).str()});
        }
        try writer.print(" ${s}(", .{self.name});
        for (self.parameters.items, 0..) |p, i| {
            if (i > 0) try writer.writeAll(", ");
            try writer.print("{s} %param_{d}", .{ p.il_type.str(), i });
        }
        try writer.writeAll(") {\n@start\n");

        // ret alloc if return type is aggregate (size > 8)
        if (rt.size_of() > 8) {
            const align_v = @max(rt.align_of(), 4);
            try writer.print("    %ret =l alloc{d} {d}\n", .{ align_v, rt.size_of() });
        }
        // Stack allocations for all bindings
        for (self.bindings.items, 0..) |b, i| {
            const bt = b.type_handle.getConst();
            const align_v = @max(bt.align_of(), 4);
            try writer.print("    %var_{d} =l alloc{d} {d}\n", .{ i, align_v, @max(bt.size_of(), 1) });
        }
        // Stack allocations for aggregate temporaries
        for (self.temporaries.items, 0..) |tmp, i| {
            if (tmp.il_type.isAggregate()) {
                const bt = tmp.type_handle.getConst();
                const align_v = @max(bt.align_of(), 4);
                try writer.print("    %temp_{d} =l alloc{d} {d}\n", .{ i, align_v, bt.size_of() });
            }
        }
        for (self.instructions.items) |inst| {
            try inst.write(writer);
        }
        // Ensure function ends with a ret
        const last_is_ret = blk: {
            if (self.instructions.items.len == 0) break :blk false;
            break :blk self.instructions.items[self.instructions.items.len - 1].isRet();
        };
        if (!last_is_ret) {
            if (rt.is_a(.void_type)) {
                try writer.writeAll("    ret\n");
            } else {
                try writer.writeAll("    ret 0\n");
            }
        }
        try writer.writeAll("}\n\n");
    }
};

// ── ILGlobal / ILFile ─────────────────────────────────────────────────────────

pub const ILGlobal = struct {
    name: []const u8,
    type_handle: TypeHandle,
    init_value: ILValue,
};

pub const ILStringEntry = struct {
    text: []const u8,
    index: usize,
};

pub const ILFile = struct {
    name: []const u8,
    functions: ArrayList(ILFunction),
    globals: ArrayList(ILGlobal),
    strings: ArrayList(ILStringEntry),
    cstrings: ArrayList(ILStringEntry),
    registered_types: ArrayList(TypeHandle),
    libraries: ArrayList([]const u8),
    has_exports: bool = false,
    alloc: Allocator,

    pub fn init(alloc: Allocator, name: []const u8) ILFile {
        return .{
            .name = name,
            .functions = ArrayList(ILFunction).init(alloc),
            .globals   = ArrayList(ILGlobal).init(alloc),
            .strings   = ArrayList(ILStringEntry).init(alloc),
            .cstrings  = ArrayList(ILStringEntry).init(alloc),
            .registered_types = ArrayList(TypeHandle).init(alloc),
            .libraries = ArrayList([]const u8).init(alloc),
            .alloc = alloc,
        };
    }

    pub fn deinit(self: *ILFile) void {
        for (self.functions.items) |*f| f.deinit();
        self.functions.deinit();
        self.globals.deinit();
        self.strings.deinit();
        self.cstrings.deinit();
        self.registered_types.deinit();
        self.libraries.deinit();
    }

    pub fn findGlobal(self: *const ILFile, name: []const u8) ?ILValue {
        for (self.globals.items) |g| {
            if (std.mem.eql(u8, g.name, name)) return ILValue.makeGlobal(name, qbeTypeCode(g.type_handle));
        }
        return null;
    }

    pub fn addString(self: *ILFile, text: []const u8) !ILValue {
        for (self.strings.items) |s| {
            if (std.mem.eql(u8, s.text, text)) return ILValue.makeGlobal(
                try std.fmt.allocPrint(self.alloc, "str_{d}", .{s.index}),
                .{ .base = .L });
        }
        const idx = self.strings.items.len;
        try self.strings.append(.{ .text = text, .index = idx });
        return ILValue.makeGlobal(
            try std.fmt.allocPrint(self.alloc, "str_{d}", .{idx}),
            .{ .base = .L });
    }

    pub fn addCString(self: *ILFile, text: []const u8) !ILValue {
        for (self.cstrings.items) |s| {
            if (std.mem.eql(u8, s.text, text)) return ILValue.makeGlobal(
                try std.fmt.allocPrint(self.alloc, "cstr_{d}", .{s.index}),
                .{ .base = .L });
        }
        const idx = self.cstrings.items.len;
        try self.cstrings.append(.{ .text = text, .index = idx });
        return ILValue.makeGlobal(
            try std.fmt.allocPrint(self.alloc, "cstr_{d}", .{idx}),
            .{ .base = .L });
    }

    pub fn registerType(self: *ILFile, th: TypeHandle) !void {
        for (self.registered_types.items) |existing| {
            if (TypeHandle.eql(existing, th)) return;
        }
        try self.registered_types.append(th);
    }

    pub fn write(self: *const ILFile, writer: anytype) !void {
        // Type definitions
        for (self.registered_types.items) |th| {
            try emitTypeDecl(th, writer, self.alloc);
        }
        if (self.registered_types.items.len > 0) try writer.writeAll("\n");

        // Debug file marker
        try writer.print("dbgfile \"{s}\"\n\n", .{self.name});

        // Functions
        for (self.functions.items) |*f| {
            try f.write(writer);
        }

        // Global data
        for (self.globals.items) |g| {
            try writer.print("data ${s} = {{ ", .{g.name});
            const il = qbeTypeCode(g.type_handle);
            try writer.writeAll(il.str());
            try writer.writeAll(" ");
            try g.init_value.write(writer);
            try writer.writeAll(" }\n");
        }
        if (self.globals.items.len > 0) try writer.writeAll("\n");

        // String data (UTF-32 slices: pointer + length)
        for (self.strings.items) |s| {
            // Emit the codepoints then a slice_t pointing to them
            const cp_name = try std.fmt.allocPrint(self.alloc, "str_cp_{d}", .{s.index});
            try writer.print("data ${s} = {{ ", .{cp_name});
            var iter = std.unicode.Utf8Iterator{ .bytes = s.text, .i = 0 };
            var count: usize = 0;
            while (iter.nextCodepoint()) |cp| {
                if (count > 0) try writer.writeAll(", ");
                try writer.print("w {d}", .{cp});
                count += 1;
            }
            try writer.writeAll(" }\n");
            try writer.print("data $str_{d} = {{ l $str_cp_{d}, l {d} }}\n", .{ s.index, s.index, count });
        }

        // CString data (zero-terminated byte arrays)
        for (self.cstrings.items) |s| {
            try writer.print("data $cstr_{d} = {{ b \"{s}\", b 0 }}\n", .{ s.index, s.text });
        }
    }
};

fn emitTypeDecl(th: TypeHandle, writer: anytype, alloc: Allocator) !void {
    const t = th.getConst();
    switch (t.description) {
        .slice_type => {
            try writer.writeAll("type :slice_t = { l, l }\n");
        },
        .optional_type => |ot| {
            const inner_ref = try typeRef(ot.type, alloc);
            try writer.print("type :opt{d} = {{ {s}, b }}\n", .{ th.index, inner_ref });
        },
        .struct_type => |st| {
            try writer.print("type :struct{d} = {{ ", .{th.index});
            for (st.fields, 0..) |f, i| {
                if (i > 0) try writer.writeAll(", ");
                try writer.writeAll(try typeRef(f.type, alloc));
            }
            try writer.writeAll(" }\n");
        },
        .result_type => |rt| {
            // Two types: a union of success/error_type, then the wrapper with flag
            const s_ref = try typeRef(rt.success, alloc);
            const e_ref = try typeRef(rt.error_type, alloc);
            try writer.print("type :res_u{d} = {{ {s}, {s} }}\n", .{ th.index, s_ref, e_ref });
            try writer.print("type :res{d} = {{ :res_u{d}, b }}\n", .{ th.index, th.index });
        },
        .tagged_union_type => |tu| {
            // Payload union + wrapper with tag
            try writer.print("type :union_p{d} = {{ ", .{th.index});
            for (tu.tags, 0..) |tag, i| {
                if (i > 0) try writer.writeAll(", ");
                if (tag.payload.isNull()) {
                    try writer.writeAll("b");
                } else {
                    try writer.writeAll(try typeRef(tag.payload, alloc));
                }
            }
            try writer.writeAll(" }\n");
            const tag_ref = try typeRef(tu.tag_type, alloc);
            try writer.print("type :union{d} = {{ :union_p{d}, {s} }}\n", .{ th.index, th.index, tag_ref });
        },
        else => {},
    }
}

// ── ILProgram ─────────────────────────────────────────────────────────────────

pub const ILProgram = struct {
    name: []const u8,
    files: ArrayList(ILFile),
    alloc: Allocator,

    pub fn init(alloc: Allocator, name: []const u8) ILProgram {
        return .{
            .name = name,
            .files = ArrayList(ILFile).init(alloc),
            .alloc = alloc,
        };
    }

    pub fn deinit(self: *ILProgram) void {
        for (self.files.items) |*f| f.deinit();
        self.files.deinit();
    }
};

// ── QBEContext ────────────────────────────────────────────────────────────────

pub const QBEContext = struct {
    arena: std.heap.ArenaAllocator,
    next_local: usize = 0,
    current_file: usize = std.math.maxInt(usize),
    current_function: usize = std.math.maxInt(usize),
    program: ILProgram,
    err_msg: []const u8 = "",
    libraries: ArrayList([]const u8),

    pub fn init(gpa: Allocator, program_name: []const u8) !QBEContext {
        var arena = std.heap.ArenaAllocator.init(gpa);
        return .{
            .arena = arena,
            .program = ILProgram.init(arena.allocator(), program_name),
            .libraries = ArrayList([]const u8).init(arena.allocator()),
        };
    }

    pub fn deinit(self: *QBEContext) void {
        self.program.deinit();
        self.arena.deinit();
    }

    pub fn file(self: *QBEContext) *ILFile {
        return &self.program.files.items[self.current_file];
    }

    pub fn function(self: *QBEContext) *ILFunction {
        return &self.file().functions.items[self.current_function];
    }

    pub fn inGlobalScope(self: *const QBEContext) bool {
        return self.current_function == std.math.maxInt(usize);
    }

    pub fn newLocal(self: *QBEContext, il: ILType) ILValue {
        const n = self.next_local;
        self.next_local += 1;
        return ILValue.makeLocal(n, il);
    }

    pub fn addOperation(self: *QBEContext, inst: ILInstruction) !void {
        const func = self.function();
        if (func.after_ret and !inst.isLabel()) return; // dead code
        if (inst.isLabel()) func.after_ret = false;
        if (inst.isRet()) func.after_ret = true;
        try func.instructions.append(inst);
    }

    pub fn addFile(self: *QBEContext, name: []const u8) !void {
        const f = ILFile.init(self.arena.allocator(), name);
        try self.program.files.append(f);
        self.current_file = self.program.files.items.len - 1;
        self.current_function = std.math.maxInt(usize);
    }

    pub fn addFunction(self: *QBEContext, name: []const u8, full_name: []const u8, return_type: TypeHandle, is_export: bool) !void {
        const f = ILFunction.init(self.arena.allocator(), name, full_name, return_type, is_export);
        try self.file().functions.append(f);
        self.current_function = self.file().functions.items.len - 1;
        self.next_local = 0;
        self.function().after_ret = false;
    }

    pub fn endFunction(self: *QBEContext) void {
        self.current_function = std.math.maxInt(usize);
    }

    pub fn registerAggregateType(self: *QBEContext, th: TypeHandle) !void {
        if (self.current_file == std.math.maxInt(usize)) return;
        try self.file().registerType(th);
    }

    pub fn addLibrary(self: *QBEContext, lib: []const u8) !void {
        for (self.libraries.items) |l| if (std.mem.eql(u8, l, lib)) return;
        try self.libraries.append(lib);
    }
};

// ── QBEOperand ────────────────────────────────────────────────────────────────

pub const GenError = error{ QbeError, OutOfMemory };

pub const QBEOperand = struct {
    node: AstNode,
    value_type: TypeHandle,
    value: ?ILValue = null,

    pub fn get(self: *QBEOperand, ctx: *QBEContext, p: *Parser) GenError!ILValue {
        if (self.value) |v| return v;
        const op = try generateNode(ctx, p, self.node);
        self.value = op.value;
        self.value_type = op.value_type;
        return op.value orelse ILValue.makeNone();
    }

    // Load a first-class value from a variable/parameter pointer, or deref a reference.
    pub fn dereference(self: *QBEOperand, ctx: *QBEContext) !void {
        const val = self.value orelse return;
        const t = self.value_type.getConst();

        if (t.description == .reference_type) {
            const inner_th = t.description.reference_type.referencing;
            const load_code  = qbeLoadCode(inner_th);
            const result_il  = qbeTypeCode(inner_th);
            const target = ctx.newLocal(result_il);
            try ctx.addOperation(.{ .load = .{ .target = target, .pointer = val, .load_type = load_code } });
            self.value = target;
            self.value_type = inner_th;
            return;
        }

        if (qbeFirstClass(self.value_type)) {
            switch (val.inner) {
                .variable, .parameter => {
                    const load_code = qbeLoadCode(self.value_type);
                    const result_il = qbeTypeCode(self.value_type);
                    const target = ctx.newLocal(result_il);
                    try ctx.addOperation(.{ .load = .{ .target = target, .pointer = val, .load_type = load_code } });
                    self.value = target;
                },
                else => {},
            }
        }
    }

    // If value is a sequence, spill it to a stack temporary and return the pointer.
    pub fn materialize(self: *QBEOperand, ctx: *QBEContext) !void {
        const val = self.value orelse return;
        if (val.inner != .sequence) return;
        const il  = try qbeType(self.value_type, ctx);
        const tmp = try ctx.function().addTemporary(self.value_type, il);
        try rawAssign(ctx, tmp, val, self.value_type);
        self.value = tmp;
    }
};

// ── rawAssign — store a value at a destination pointer ────────────────────────

fn rawAssign(ctx: *QBEContext, dest_ptr: ILValue, src_val: ILValue, th: TypeHandle) !void {
    const t = th.getConst();
    switch (t.description) {
        .struct_type => |st| {
            switch (src_val.inner) {
                .sequence => |vals| {
                    var offset: usize = 0;
                    for (st.fields, vals) |field, fval| {
                        const ft = field.type.getConst();
                        offset = alignAt(offset, ft.align_of());
                        const fptr = ctx.newLocal(.{ .base = .L });
                        try ctx.addOperation(.{ .expr = .{
                            .target = fptr,
                            .lhs    = dest_ptr,
                            .rhs    = ILValue.makeInteger(@intCast(offset), .{ .base = .L }),
                            .op     = .add,
                        }});
                        try rawAssign(ctx, fptr, fval, field.type);
                        offset += ft.size_of();
                    }
                },
                else => {
                    try ctx.addOperation(.{ .blit = .{ .src = src_val, .dest = dest_ptr, .bytes = t.size_of() }});
                },
            }
        },
        .optional_type, .result_type, .tagged_union_type, .slice_type => {
            try ctx.addOperation(.{ .blit = .{ .src = src_val, .dest = dest_ptr, .bytes = t.size_of() }});
        },
        else => {
            try ctx.addOperation(.{ .store = .{ .value = src_val, .pointer = dest_ptr }});
        },
    }
}

// assign — generate rhs and store it to lhs_ptr
fn assign(ctx: *QBEContext, p: *Parser, lhs_ptr: ILValue, lhs_th: TypeHandle, rhs_node: AstNode) GenError!void {
    var rhs_op = try generateNode(ctx, p, rhs_node);
    try rhs_op.dereference(ctx);

    const lhs_t = lhs_th.getConst();
    const rhs_val = rhs_op.value orelse ILValue.makeNone();

    switch (lhs_t.description) {
        .optional_type => |ot| {
            const rhs_t = rhs_op.value_type.getConst();
            if (rhs_t.is_a(.void_type)) {
                // null → set flag byte to 0
                const flag_ptr = ctx.newLocal(.{ .base = .L });
                const flag_off: i64 = @intCast(ot.type.getConst().size_of());
                try ctx.addOperation(.{ .expr = .{
                    .target = flag_ptr,
                    .lhs    = lhs_ptr,
                    .rhs    = ILValue.makeInteger(flag_off, .{ .base = .L }),
                    .op     = .add,
                }});
                try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(0, .{ .base = .B }), .pointer = flag_ptr }});
            } else {
                // inner value → store payload then set flag = 1
                try rawAssign(ctx, lhs_ptr, rhs_val, ot.type);
                const flag_ptr = ctx.newLocal(.{ .base = .L });
                const flag_off: i64 = @intCast(ot.type.getConst().size_of());
                try ctx.addOperation(.{ .expr = .{
                    .target = flag_ptr,
                    .lhs    = lhs_ptr,
                    .rhs    = ILValue.makeInteger(flag_off, .{ .base = .L }),
                    .op     = .add,
                }});
                try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(1, .{ .base = .B }), .pointer = flag_ptr }});
            }
        },
        .result_type => |rt| {
            const rhs_t = rhs_op.value_type.getConst();
            const flag_off: i64 = @intCast(rt.flag_offset());
            const flag_ptr = ctx.newLocal(.{ .base = .L });
            try ctx.addOperation(.{ .expr = .{
                .target = flag_ptr,
                .lhs    = lhs_ptr,
                .rhs    = ILValue.makeInteger(flag_off, .{ .base = .L }),
                .op     = .add,
            }});
            if (TypeHandle.eql(rhs_op.value_type, rt.success)) {
                try rawAssign(ctx, lhs_ptr, rhs_val, rt.success);
                try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(1, .{ .base = .B }), .pointer = flag_ptr }});
            } else if (TypeHandle.eql(rhs_op.value_type, rt.error_type)) {
                // Store to error slot (at the same start address as the union)
                const err_size: i64 = @intCast(rhs_t.size_of());
                _ = err_size;
                try rawAssign(ctx, lhs_ptr, rhs_val, rt.error_type);
                try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(0, .{ .base = .B }), .pointer = flag_ptr }});
            } else {
                try rawAssign(ctx, lhs_ptr, rhs_val, lhs_th);
            }
        },
        else => try rawAssign(ctx, lhs_ptr, rhs_val, lhs_th),
    }
}

// ── Generate functions ────────────────────────────────────────────────────────

pub fn generateNode(ctx: *QBEContext, p: *Parser, node: AstNode) GenError!QBEOperand {
    if (node.isNull()) return QBEOperand{ .node = node, .value_type = type_mod.the().void_type, .value = ILValue.makeNone() };
    const impl = node.getConst();
    return switch (impl.node) {
        .number           => |n| genNumber(ctx, node, n),
        .bool_constant    => |n| genBoolConst(ctx, node, n),
        .decimal          => |n| genDecimal(ctx, node, n),
        .null_ptr         => genNullPtr(ctx, node),
        .void_node, .dummy => genVoid(ctx, node),
        .cstring          => |n| try genCString(ctx, p, node, n),
        .string           => |n| try genString(ctx, p, node, n),
        .quoted_string    => |n| try genQuotedString(ctx, p, node, n),
        .identifier       => |n| genIdentifier(ctx, node, n),
        .block            => |n| try genBlock(ctx, p, node, n),
        .function_definition => |n| try genFunctionDefinition(ctx, p, node, n),
        .function_declaration => |n| try genFunctionDeclaration(ctx, p, node, n),
        .variable_declaration => |n| try genVariableDeclaration(ctx, p, node, n),
        .binary_expression => |n| try genBinaryExpression(ctx, p, node, n),
        .unary_expression  => |n| try genUnaryExpression(ctx, p, node, n),
        .@"return"        => |n| try genReturn(ctx, p, node, n),
        .if_statement     => |n| try genIfStatement(ctx, p, node, n),
        .while_statement  => |n| try genWhileStatement(ctx, p, node, n),
        .loop_statement   => |n| try genLoopStatement(ctx, p, node, n),
        .for_statement    => |n| try genForStatement(ctx, p, node, n),
        .@"break"         => |n| try genBreak(ctx, p, node, n),
        .call             => |n| try genCall(ctx, p, node, n),
        .argument_list    => |n| try genArgumentList(ctx, p, node, n),
        .expression_list  => |n| try genExpressionList(ctx, p, node, n),
        .module           => |n| try genModule(ctx, p, node, n),
        .program          => |n| try genProgram(ctx, p, node, n),
        .@"extern"        => |n| try genExtern(ctx, node, n),
        .public_declaration  => |n| try genPublicDeclaration(ctx, p, node, n),
        .export_declaration  => |n| try genExportDeclaration(ctx, p, node, n),
        .switch_statement => |n| try genSwitchStatement(ctx, p, node, n),
        .tag_value        => |n| try genTagValue(ctx, p, node, n),
        // Type declarations produce no IL
        .@"enum", .@"struct", .alias => genTypeDecl(ctx, node),
        // Unimplemented / ignored
        else => QBEOperand{ .node = node, .value_type = impl.bound_type, .value = ILValue.makeNone() },
    };
}

fn voidOp(node: AstNode) QBEOperand {
    return .{ .node = node, .value_type = type_mod.the().void_type, .value = ILValue.makeNone() };
}

fn genVoid(ctx: *QBEContext, node: AstNode) QBEOperand {
    _ = ctx;
    return voidOp(node);
}
fn genTypeDecl(ctx: *QBEContext, node: AstNode) QBEOperand {
    _ = ctx;
    return voidOp(node);
}

fn genNullPtr(ctx: *QBEContext, node: AstNode) QBEOperand {
    _ = ctx;
    return .{ .node = node, .value_type = node.getConst().bound_type, .value = ILValue.makeInteger(0, .{ .base = .L }) };
}

fn genNumber(ctx: *QBEContext, node: AstNode, n: sn.Number) QBEOperand {
    _ = ctx;
    const th = node.getConst().bound_type;
    const il = qbeTypeCode(th);
    const v: i64 = switch (n.value) {
        .u64 => |x| @as(i64, @bitCast(x)),
        .i64 => |x| x,
        .u32 => |x| @intCast(x),
        .i32 => |x| x,
        .u16 => |x| @intCast(x),
        .i16 => |x| x,
        .u8  => |x| @intCast(x),
        .i8  => |x| x,
    };
    return .{ .node = node, .value_type = th, .value = ILValue.makeInteger(v, il) };
}

fn genBoolConst(ctx: *QBEContext, node: AstNode, n: sn.BoolConstant) QBEOperand {
    _ = ctx;
    const th = node.getConst().bound_type;
    return .{ .node = node, .value_type = th, .value = ILValue.makeInteger(if (n.value) 1 else 0, .{ .base = .B }) };
}

fn genDecimal(ctx: *QBEContext, node: AstNode, n: sn.Decimal) QBEOperand {
    _ = ctx;
    const th = node.getConst().bound_type;
    const il = qbeTypeCode(th);
    return .{ .node = node, .value_type = th, .value = ILValue.makeFloat(n.value, il) };
}

fn genCString(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.CString) !QBEOperand {
    _ = p;
    const th = node.getConst().bound_type;
    const val = try ctx.file().addCString(n.string);
    return .{ .node = node, .value_type = th, .value = val };
}

fn genString(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.String) !QBEOperand {
    _ = p;
    const th = node.getConst().bound_type;
    const val = try ctx.file().addString(n.string);
    return .{ .node = node, .value_type = th, .value = val };
}

fn genQuotedString(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.QuotedString) !QBEOperand {
    _ = p;
    const th = node.getConst().bound_type;
    if (n.quote_type == .single) {
        const val = try ctx.file().addCString(n.string);
        return .{ .node = node, .value_type = th, .value = val };
    }
    const val = try ctx.file().addString(n.string);
    return .{ .node = node, .value_type = th, .value = val };
}

fn genIdentifier(ctx: *QBEContext, node: AstNode, n: sn.Identifier) QBEOperand {
    const th = node.getConst().bound_type;
    const il = qbeTypeCode(th);

    if (!ctx.inGlobalScope()) {
        const func = ctx.function();
        // Check parameter first — parameters are stored in stack vars
        if (func.findParameter(n.identifier)) |found| {
            // Return the stack var if it exists, else the parameter directly
            const p = func.parameters.items[blk: {
                for (func.parameters.items, 0..) |param, i| {
                    if (std.mem.eql(u8, param.name, n.identifier)) break :blk i;
                }
                break :blk 0;
            }];
            if (p.var_index) |vi| {
                return .{ .node = node, .value_type = found.th, .value = ILValue.makeVariable(vi, found.val.il_type) };
            }
            return .{ .node = node, .value_type = found.th, .value = found.val };
        }
        if (func.findBinding(n.identifier)) |val| {
            return .{ .node = node, .value_type = th, .value = val };
        }
    }

    // Check file globals
    if (ctx.file().findGlobal(n.identifier)) |val| {
        return .{ .node = node, .value_type = th, .value = val };
    }

    // Fall back to a global symbol (function name etc.)
    return .{ .node = node, .value_type = th, .value = ILValue.makeGlobal(n.identifier, il) };
}

fn genBlock(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Block) !QBEOperand {
    _ = node;
    var last = voidOp(AstNode.null_handle);
    for (n.statements) |stmt| {
        last = try generateNode(ctx, p, stmt);
    }
    return last;
}

fn genExpressionList(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.ExpressionList) !QBEOperand {
    _ = node;
    var last = voidOp(AstNode.null_handle);
    for (n.expressions) |expr| {
        last = try generateNode(ctx, p, expr);
    }
    return last;
}

fn genArgumentList(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.ArgumentList) !QBEOperand {
    // Build a sequence value from all arguments
    var vals = ArrayList(ILValue).init(ctx.arena.allocator());
    for (n.arguments) |arg| {
        var op = try generateNode(ctx, p, arg);
        try op.dereference(ctx);
        try vals.append(op.value orelse ILValue.makeNone());
    }
    const th = node.getConst().bound_type;
    const il = qbeTypeCode(th);
    return .{ .node = node, .value_type = th, .value = ILValue.makeSequence(vals.items, il) };
}

fn genReturn(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Return) !QBEOperand {
    _ = node;
    if (n.expression.isNull()) {
        try ctx.addOperation(.{ .ret = .{ .value = ILValue.makeNone() } });
        return voidOp(AstNode.null_handle);
    }
    var op = try generateNode(ctx, p, n.expression);
    try op.dereference(ctx);
    const val = op.value orelse ILValue.makeNone();
    // If return type is large, we have a %ret alloc — blit into it then ret %ret
    const rt = ctx.function().return_type.getConst();
    if (rt.size_of() > 8) {
        try rawAssign(ctx, ILValue.makeReturnValue(.{ .base = .L }), val, ctx.function().return_type);
        try ctx.addOperation(.{ .ret = .{ .value = ILValue.makeReturnValue(.{ .base = .L }) } });
    } else {
        try ctx.addOperation(.{ .ret = .{ .value = val } });
    }
    return voidOp(AstNode.null_handle);
}

fn genVariableDeclaration(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.VariableDeclaration) !QBEOperand {
    const th = node.getConst().bound_type;
    const il = qbeTypeCode(th);

    if (ctx.inGlobalScope()) {
        if (n.is_const) return voidOp(node);
        var init_val = ILValue.makeInteger(0, il);
        if (!n.initializer.isNull()) {
            var op = try generateNode(ctx, p, n.initializer);
            try op.dereference(ctx);
            init_val = op.value orelse init_val;
        }
        try ctx.file().globals.append(.{ .name = n.name, .type_handle = th, .init_value = init_val });
        return .{ .node = node, .value_type = th, .value = ILValue.makeGlobal(n.name, il) };
    }

    // Local: const declarations with no side effects are skipped
    if (n.is_const) {
        if (n.initializer.isNull()) return voidOp(node);
        // Still generate the initializer for side effects
        _ = try generateNode(ctx, p, n.initializer);
        return voidOp(node);
    }

    const var_val = try ctx.function().addBinding(n.name, th);
    if (!n.initializer.isNull()) {
        try assign(ctx, p, var_val, th, n.initializer);
    }
    return .{ .node = node, .value_type = th, .value = var_val };
}

fn genIfStatement(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.IfStatement) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const true_lbl = try std.fmt.allocPrint(alloc, "if_{d}", .{idx});
    const else_lbl = try std.fmt.allocPrint(alloc, "else_{d}", .{idx});
    const end_lbl  = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});

    var cond = try generateNode(ctx, p, n.condition);
    try cond.dereference(ctx);
    const cond_val = cond.value orelse ILValue.makeInteger(0, .{ .base = .W });

    const has_else = !n.else_branch.isNull() and n.else_branch.getConst().node != .void_node;
    const false_lbl = if (has_else) else_lbl else end_lbl;

    try ctx.addOperation(.{ .jnz = .{ .condition = cond_val, .on_true = true_lbl, .on_false = false_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = true_lbl } });
    _ = try generateNode(ctx, p, n.if_branch);
    try ctx.addOperation(.{ .jmp = .{ .label = end_lbl } });

    if (has_else) {
        try ctx.addOperation(.{ .label = .{ .name = else_lbl } });
        _ = try generateNode(ctx, p, n.else_branch);
        try ctx.addOperation(.{ .jmp = .{ .label = end_lbl } });
    }
    try ctx.addOperation(.{ .label = .{ .name = end_lbl } });
    return voidOp(node);
}

fn genWhileStatement(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.WhileStatement) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const top_lbl  = try std.fmt.allocPrint(alloc, "top_{d}", .{idx});
    const body_lbl = try std.fmt.allocPrint(alloc, "body_{d}", .{idx});
    const end_lbl  = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});

    try ctx.addOperation(.{ .jmp = .{ .label = top_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = top_lbl } });
    var cond = try generateNode(ctx, p, n.condition);
    try cond.dereference(ctx);
    const cond_val = cond.value orelse ILValue.makeInteger(0, .{ .base = .W });
    try ctx.addOperation(.{ .jnz = .{ .condition = cond_val, .on_true = body_lbl, .on_false = end_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = body_lbl } });
    _ = try generateNode(ctx, p, n.statement);
    try ctx.addOperation(.{ .jmp = .{ .label = top_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = end_lbl } });
    return voidOp(node);
}

fn genLoopStatement(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.LoopStatement) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const top_lbl = try std.fmt.allocPrint(alloc, "top_{d}", .{idx});
    const end_lbl = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});

    try ctx.addOperation(.{ .jmp = .{ .label = top_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = top_lbl } });
    _ = try generateNode(ctx, p, n.statement);
    try ctx.addOperation(.{ .jmp = .{ .label = top_lbl } });
    try ctx.addOperation(.{ .label = .{ .name = end_lbl } });
    return voidOp(node);
}

fn genBreak(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Break) !QBEOperand {
    _ = p;
    // Jump to end_N where N is the break target block's resolved index
    const alloc = ctx.arena.allocator();
    const idx = if (!n.block.isNull()) n.block.resolve().index else node.resolve().index;
    const end_lbl = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});
    try ctx.addOperation(.{ .jmp = .{ .label = end_lbl } });
    return voidOp(node);
}

fn genForStatement(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.ForStatement) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const top_lbl  = try std.fmt.allocPrint(alloc, "top_{d}", .{idx});
    const body_lbl = try std.fmt.allocPrint(alloc, "body_{d}", .{idx});
    const end_lbl  = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});

    // Generate the range expression
    var range_op = try generateNode(ctx, p, n.range_expr);
    const range_th = range_op.value_type;
    const range_t  = range_th.getConst();

    // Enumerate over a range_type (has start, step, end fields)
    if (range_t.description == .range_type) {
        const elem_th = range_t.description.range_type.range_of;
        const il = qbeTypeCode(elem_th);
        // Get start, step, end from the range value
        try range_op.materialize(ctx);
        const range_ptr = range_op.value orelse return voidOp(node);
        const lc = qbeLoadCode(elem_th);

        const start_ptr = ctx.newLocal(.{ .base = .L });
        const step_ptr  = ctx.newLocal(.{ .base = .L });
        const end_ptr   = ctx.newLocal(.{ .base = .L });
        const elem_sz: i64 = @intCast(elem_th.getConst().size_of());
        try ctx.addOperation(.{ .expr = .{ .target = start_ptr, .lhs = range_ptr, .rhs = ILValue.makeInteger(0, .{ .base = .L }), .op = .add }});
        try ctx.addOperation(.{ .expr = .{ .target = step_ptr, .lhs = range_ptr, .rhs = ILValue.makeInteger(elem_sz, .{ .base = .L }), .op = .add }});
        try ctx.addOperation(.{ .expr = .{ .target = end_ptr, .lhs = range_ptr, .rhs = ILValue.makeInteger(elem_sz * 2, .{ .base = .L }), .op = .add }});

        // Allocate the loop variable
        const var_val = try ctx.function().addBinding(n.range_variable, elem_th);
        // Load start into var
        const start_val = ctx.newLocal(il);
        try ctx.addOperation(.{ .load = .{ .target = start_val, .pointer = start_ptr, .load_type = lc }});
        try ctx.addOperation(.{ .store = .{ .value = start_val, .pointer = var_val }});

        try ctx.addOperation(.{ .jmp = .{ .label = top_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = top_lbl }});
        // Condition: var < end
        const cur_val = ctx.newLocal(il);
        try ctx.addOperation(.{ .load = .{ .target = cur_val, .pointer = var_val, .load_type = lc }});
        const end_val = ctx.newLocal(il);
        try ctx.addOperation(.{ .load = .{ .target = end_val, .pointer = end_ptr, .load_type = lc }});
        const cmp_val = ctx.newLocal(.{ .base = .W });
        try ctx.addOperation(.{ .expr = .{ .target = cmp_val, .lhs = cur_val, .rhs = end_val, .op = .cmp_slt }});
        try ctx.addOperation(.{ .jnz = .{ .condition = cmp_val, .on_true = body_lbl, .on_false = end_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = body_lbl }});
        _ = try generateNode(ctx, p, n.statement);
        // Increment: var += step
        const step_val = ctx.newLocal(il);
        try ctx.addOperation(.{ .load = .{ .target = step_val, .pointer = step_ptr, .load_type = lc }});
        const next_val = ctx.newLocal(il);
        try ctx.addOperation(.{ .expr = .{ .target = next_val, .lhs = cur_val, .rhs = step_val, .op = .add }});
        try ctx.addOperation(.{ .store = .{ .value = next_val, .pointer = var_val }});
        try ctx.addOperation(.{ .jmp = .{ .label = top_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = end_lbl }});
        return voidOp(node);
    }

    // Enumerate over an enum type
    if (range_t.description == .enum_type) {
        const enum_descr = range_t.description.enum_type;
        const ul = qbeTypeCode(enum_descr.underlying_type);
        const var_val = try ctx.function().addBinding(n.range_variable, range_th);

        if (enum_descr.values.len == 0) return voidOp(node);

        // Initialise with first enum value
        try ctx.addOperation(.{ .store = .{
            .value = ILValue.makeInteger(enum_descr.values[0].value, ul),
            .pointer = var_val,
        }});

        // Emit the iteration (an indexed loop over enum values)
        // We use a counter index and switch on it
        const idx_il: ILType = .{ .base = .L };
        const idx_var = try ctx.function().addBinding("__enum_idx", type_mod.the().i64_type);
        try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(0, idx_il), .pointer = idx_var }});
        const count: i64 = @intCast(enum_descr.values.len);

        try ctx.addOperation(.{ .jmp = .{ .label = top_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = top_lbl }});
        const cur_idx = ctx.newLocal(idx_il);
        try ctx.addOperation(.{ .load = .{ .target = cur_idx, .pointer = idx_var, .load_type = .L }});
        const cmp_val = ctx.newLocal(.{ .base = .W });
        try ctx.addOperation(.{ .expr = .{ .target = cmp_val, .lhs = cur_idx, .rhs = ILValue.makeInteger(count, idx_il), .op = .cmp_slt }});
        try ctx.addOperation(.{ .jnz = .{ .condition = cmp_val, .on_true = body_lbl, .on_false = end_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = body_lbl }});
        _ = try generateNode(ctx, p, n.statement);
        // Increment index, load next enum value
        const next_idx = ctx.newLocal(idx_il);
        try ctx.addOperation(.{ .expr = .{ .target = next_idx, .lhs = cur_idx, .rhs = ILValue.makeInteger(1, idx_il), .op = .add }});
        try ctx.addOperation(.{ .store = .{ .value = next_idx, .pointer = idx_var }});
        try ctx.addOperation(.{ .jmp = .{ .label = top_lbl }});
        try ctx.addOperation(.{ .label = .{ .name = end_lbl }});
        return voidOp(node);
    }

    return voidOp(node);
}

fn genCall(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Call) !QBEOperand {
    const th = node.getConst().bound_type;

    // Resolve function name
    var func_name: []const u8 = "";
    if (!n.function.isNull()) {
        const fimpl = n.function.getConst();
        switch (fimpl.node) {
            .function_definition => |fd| {
                // Check for ExternLink in declaration
                const decl = fd.declaration.getConst();
                if (decl.node == .function_declaration) {
                    // Check the ns for extern_link tag (simplified: use name as-is)
                }
                func_name = fd.name;
            },
            .extern_link => |el| func_name = el.link_name,
            else => func_name = sn.nodeName(n.function),
        }
    }
    if (func_name.len == 0) {
        // callable expression — best-effort: generate and look for a global name
        var callee_op = try generateNode(ctx, p, n.callable);
        try callee_op.dereference(ctx);
        if (callee_op.value) |v| switch (v.inner) {
            .global => |name| { func_name = name; },
            else => {},
        };
        if (func_name.len == 0) func_name = "unknown";
    }

    // Generate arguments
    var args = ArrayList(CallArg).init(ctx.arena.allocator());
    if (!n.arguments.isNull()) {
        const anode = n.arguments.getConst();
        switch (anode.node) {
            .argument_list => |al| {
                for (al.arguments) |arg_node| {
                    var aop = try generateNode(ctx, p, arg_node);
                    try aop.dereference(ctx);
                    const arg_th = aop.value_type;
                    const ext_il: ILType = .{ .base = qbeTypeCode(arg_th).asBase().mustExtend() };
                    try args.append(.{ .il_type = ext_il, .value = aop.value orelse ILValue.makeNone() });
                }
            },
            else => {
                var aop = try generateNode(ctx, p, n.arguments);
                try aop.dereference(ctx);
                const ext_il: ILType = .{ .base = qbeTypeCode(aop.value_type).asBase().mustExtend() };
                try args.append(.{ .il_type = ext_il, .value = aop.value orelse ILValue.makeNone() });
            },
        }
    }

    const rt = th.getConst();
    if (rt.is_a(.void_type)) {
        try ctx.addOperation(.{ .call = .{
            .target    = ILValue.makeNone(),
            .ret_type  = .{ .base = .V },
            .func_name = func_name,
            .full_name = func_name,
            .args      = args.items,
        }});
        return voidOp(node);
    }

    const ret_il = qbeTypeCode(th);
    const target = ctx.newLocal(ret_il);
    try ctx.addOperation(.{ .call = .{
        .target    = target,
        .ret_type  = ret_il,
        .func_name = func_name,
        .full_name = func_name,
        .args      = args.items,
    }});
    return .{ .node = node, .value_type = th, .value = target };
}

fn genFunctionDeclaration(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.FunctionDeclaration) !QBEOperand {
    _ = p; _ = node; _ = n;
    // Parameter registration is handled by genFunctionDefinition via addParameter.
    _ = ctx;
    return voidOp(AstNode.null_handle);
}

fn genFunctionDefinition(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.FunctionDefinition) !QBEOperand {
    // Skip extern functions — no IL body
    if (!n.declaration.isNull()) {
        const decl = n.declaration.getConst();
        if (decl.node == .extern_link) return voidOp(node);
    }

    const th = node.getConst().bound_type;
    const return_th = blk: {
        if (th.isNull()) break :blk type_mod.the().void_type;
        if (th.getConst().description == .function_type) {
            break :blk th.getConst().description.function_type.result;
        }
        break :blk type_mod.the().void_type;
    };

    const is_export = n.visibility == .export_vis or n.visibility == .public;
    if (is_export) ctx.file().has_exports = true;

    try ctx.addFunction(n.name, n.name, return_th, is_export);

    // Register parameters from declaration
    if (!n.declaration.isNull()) {
        const decl_impl = n.declaration.getConst();
        if (decl_impl.node == .function_declaration) {
            const fd = decl_impl.node.function_declaration;
            for (fd.parameters) |param_node| {
                const pi = param_node.getConst();
                if (pi.node != .parameter) continue;
                const param = pi.node.parameter;
                const param_th = pi.bound_type;
                const param_val = try ctx.function().addParameter(param.name, param_th);
                // Copy parameter to stack var (unless it's a reference)
                const pdata = ctx.function().parameters.items[ctx.function().parameters.items.len - 1];
                if (pdata.var_index) |vi| {
                    const var_ptr = ILValue.makeVariable(vi, param_val.il_type);
                    const bt = param_val.il_type.asBase().basetype();
                    try ctx.addOperation(.{ .store = .{ .value = param_val, .pointer = var_ptr } });
                    _ = bt;
                }
            }
        }
    }

    // Generate body
    if (!n.implementation.isNull()) {
        _ = try generateNode(ctx, p, n.implementation);
    }

    ctx.endFunction();
    return voidOp(node);
}

fn genModule(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Module) !QBEOperand {
    // Ensure a file exists for this module
    if (ctx.current_file == std.math.maxInt(usize)) {
        try ctx.addFile(n.source);
    }
    for (n.statements) |stmt| {
        _ = try generateNode(ctx, p, stmt);
    }
    return voidOp(node);
}

fn genProgram(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.Program) !QBEOperand {
    // Create the default file for the program
    try ctx.addFile(n.source);
    for (n.statements) |stmt| {
        _ = try generateNode(ctx, p, stmt);
    }
    return voidOp(node);
}

fn genExtern(ctx: *QBEContext, node: AstNode, n: sn.Extern) !QBEOperand {
    if (n.library.len > 0) {
        try ctx.addLibrary(n.library);
    }
    return voidOp(node);
}

fn genPublicDeclaration(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.PublicDeclaration) !QBEOperand {
    _ = node; _ = n;
    _ = ctx; _ = p;
    return voidOp(AstNode.null_handle);
}

fn genExportDeclaration(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.ExportDeclaration) !QBEOperand {
    _ = node;
    return generateNode(ctx, p, n.declaration);
}

fn genSwitchStatement(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.SwitchStatement) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const end_lbl = try std.fmt.allocPrint(alloc, "end_{d}", .{idx});

    var sv_op = try generateNode(ctx, p, n.switch_value);
    try sv_op.dereference(ctx);
    const sv_val = sv_op.value orelse ILValue.makeInteger(0, .{ .base = .W });

    // Generate a chain of comparisons for each case
    for (n.switch_cases, 0..) |case_node, ci| {
        const case_impl = case_node.getConst();
        if (case_impl.node != .switch_case) continue;
        const sc = case_impl.node.switch_case;
        const case_lbl  = try std.fmt.allocPrint(alloc, "case_{d}_{d}", .{ idx, ci });
        const next_lbl  = try std.fmt.allocPrint(alloc, "case_{d}_{d}", .{ idx, ci + 1 });

        const cv_impl = sc.case_value.getConst();
        if (cv_impl.node == .default_switch_value) {
            // Default case: always jump here
            try ctx.addOperation(.{ .label = .{ .name = case_lbl }});
            _ = try generateNode(ctx, p, sc.statement);
            try ctx.addOperation(.{ .jmp = .{ .label = end_lbl }});
        } else {
            var cv_op = try generateNode(ctx, p, sc.case_value);
            try cv_op.dereference(ctx);
            const cv_val = cv_op.value orelse ILValue.makeInteger(0, .{ .base = .W });
            const cmp = ctx.newLocal(.{ .base = .W });
            try ctx.addOperation(.{ .expr = .{ .target = cmp, .lhs = sv_val, .rhs = cv_val, .op = .cmp_eq }});
            try ctx.addOperation(.{ .jnz = .{ .condition = cmp, .on_true = case_lbl, .on_false = next_lbl }});
            try ctx.addOperation(.{ .label = .{ .name = case_lbl }});
            _ = try generateNode(ctx, p, sc.statement);
            try ctx.addOperation(.{ .jmp = .{ .label = end_lbl }});
            // Emit placeholder next_lbl (will be overwritten or jumped past)
            try ctx.addOperation(.{ .label = .{ .name = next_lbl }});
        }
    }
    try ctx.addOperation(.{ .label = .{ .name = end_lbl }});
    return voidOp(node);
}

fn genTagValue(ctx: *QBEContext, p: *Parser, node: AstNode, n: sn.TagValue) !QBEOperand {
    const th = node.getConst().bound_type;
    const il  = try qbeType(th, ctx);
    const tmp = try ctx.function().addTemporary(th, il);

    const tu_t = th.getConst().description.tagged_union_type;
    const tag_off: i64 = @intCast(tu_t.tagOffset());

    // Store payload
    if (!n.payload.isNull() and n.payload.getConst().node != .void_node) {
        var pay_op = try generateNode(ctx, p, n.payload);
        try pay_op.dereference(ctx);
        const pay_val = pay_op.value orelse ILValue.makeNone();
        try rawAssign(ctx, tmp, pay_val, n.payload_type);
    }

    // Store tag value
    const tag_ptr = ctx.newLocal(.{ .base = .L });
    try ctx.addOperation(.{ .expr = .{
        .target = tag_ptr,
        .lhs    = tmp,
        .rhs    = ILValue.makeInteger(tag_off, .{ .base = .L }),
        .op     = .add,
    }});
    const tag_il = qbeTypeCode(tu_t.tag_type);
    try ctx.addOperation(.{ .store = .{
        .value   = ILValue.makeInteger(n.tag_value, tag_il),
        .pointer = tag_ptr,
    }});
    return .{ .node = node, .value_type = th, .value = tmp };
}

fn genBinaryExpression(ctx: *QBEContext, p: *Parser, node: AstNode, be: sn.BinaryExpression) !QBEOperand {
    const th = node.getConst().bound_type;

    // Assignment — do not dereference lhs
    if (be.op == .assign) {
        const lhs_op = try generateNode(ctx, p, be.lhs);
        const lhs_ptr = lhs_op.value orelse return voidOp(node);
        try assign(ctx, p, lhs_ptr, lhs_op.value_type, be.rhs);
        return lhs_op;
    }

    // Compound assignments
    if (compoundAssignOp(be.op)) |base_op| {
        const lhs_op = try generateNode(ctx, p, be.lhs);
        const lhs_ptr = lhs_op.value orelse return voidOp(node);
        var lhs_val = lhs_op;
        try lhs_val.dereference(ctx);
        var rhs_op = try generateNode(ctx, p, be.rhs);
        try rhs_op.dereference(ctx);
        const il = qbeTypeCode(th);
        const result = ctx.newLocal(il);
        try ctx.addOperation(.{ .expr = .{
            .target = result,
            .lhs    = lhs_val.value orelse ILValue.makeNone(),
            .rhs    = rhs_op.value orelse ILValue.makeNone(),
            .op     = base_op,
        }});
        try ctx.addOperation(.{ .store = .{ .value = result, .pointer = lhs_ptr }});
        return .{ .node = node, .value_type = th, .value = result };
    }

    // Member access (struct.field or module.member)
    if (be.op == .member_access) {
        return genMemberAccess(ctx, p, node, be, th);
    }

    // Logical short-circuit operators
    if (be.op == .logical_and) return genLogicalAnd(ctx, p, node, be);
    if (be.op == .logical_or)  return genLogicalOr(ctx, p, node, be);

    // General binary expression
    var lhs_op = try generateNode(ctx, p, be.lhs);
    try lhs_op.dereference(ctx);
    var rhs_op = try generateNode(ctx, p, be.rhs);
    try rhs_op.dereference(ctx);

    const lhs_val = lhs_op.value orelse return voidOp(node);
    const rhs_val = rhs_op.value orelse return voidOp(node);
    const il = qbeTypeCode(th);
    const op  = operatorToIL(be.op, lhs_op.value_type) orelse return voidOp(node);
    const result = ctx.newLocal(il);
    try ctx.addOperation(.{ .expr = .{ .target = result, .lhs = lhs_val, .rhs = rhs_val, .op = op }});
    return .{ .node = node, .value_type = th, .value = result };
}

fn genMemberAccess(ctx: *QBEContext, p: *Parser, node: AstNode, be: sn.BinaryExpression, th: TypeHandle) GenError!QBEOperand {
    const lhs_th = be.lhs.getConst().bound_type;
    const lhs_t  = lhs_th.getConst();

    // Module member access → generate rhs directly
    if (lhs_t.description == .module_type) {
        return generateNode(ctx, p, be.rhs);
    }

    // TypeType member access → enum value lookup (already resolved in bound_type tag)
    if (lhs_t.description == .type_type) {
        const impl = node.getConst();
        const tag = impl.tag;
        return switch (tag) {
            .int_val => |v| .{ .node = node, .value_type = th, .value = ILValue.makeInteger(v, qbeTypeCode(th)) },
            else => voidOp(node),
        };
    }

    // Struct field access: add offset to base pointer
    if (lhs_t.description == .struct_type) {
        const lhs_op = try generateNode(ctx, p, be.lhs);
        const lhs_ptr = lhs_op.value orelse return voidOp(node);
        const rhs_impl = be.rhs.getConst();
        const field_name = switch (rhs_impl.node) {
            .identifier => |id| id.identifier,
            else => return voidOp(node),
        };
        const offset = lhs_t.description.struct_type.offsetOf(field_name) orelse return voidOp(node);
        if (offset == 0) {
            return .{ .node = node, .value_type = th, .value = lhs_ptr };
        }
        const field_ptr = ctx.newLocal(.{ .base = .L });
        try ctx.addOperation(.{ .expr = .{
            .target = field_ptr,
            .lhs    = lhs_ptr,
            .rhs    = ILValue.makeInteger(@intCast(offset), .{ .base = .L }),
            .op     = .add,
        }});
        return .{ .node = node, .value_type = th, .value = field_ptr };
    }

    return voidOp(node);
}

fn genLogicalAnd(ctx: *QBEContext, p: *Parser, node: AstNode, be: sn.BinaryExpression) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const rhs_lbl = try std.fmt.allocPrint(alloc, "and_rhs_{d}", .{idx});
    const end_lbl = try std.fmt.allocPrint(alloc, "and_end_{d}", .{idx});

    const result = try ctx.function().addBinding("__and_tmp", type_mod.the().boolean);
    try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(0, .{ .base = .B }), .pointer = result }});

    var lhs_op = try generateNode(ctx, p, be.lhs);
    try lhs_op.dereference(ctx);
    const lv = lhs_op.value orelse ILValue.makeInteger(0, .{ .base = .W });
    try ctx.addOperation(.{ .jnz = .{ .condition = lv, .on_true = rhs_lbl, .on_false = end_lbl }});
    try ctx.addOperation(.{ .label = .{ .name = rhs_lbl }});
    var rhs_op = try generateNode(ctx, p, be.rhs);
    try rhs_op.dereference(ctx);
    const rv = rhs_op.value orelse ILValue.makeInteger(0, .{ .base = .W });
    const rv_b = ctx.newLocal(.{ .base = .B });
    try ctx.addOperation(.{ .expr = .{ .target = rv_b, .lhs = rv, .rhs = ILValue.makeInteger(0, .{ .base = .W }), .op = .cmp_ne }});
    try ctx.addOperation(.{ .store = .{ .value = rv_b, .pointer = result }});
    try ctx.addOperation(.{ .jmp = .{ .label = end_lbl }});
    try ctx.addOperation(.{ .label = .{ .name = end_lbl }});
    return .{ .node = node, .value_type = type_mod.the().boolean, .value = result };
}

fn genLogicalOr(ctx: *QBEContext, p: *Parser, node: AstNode, be: sn.BinaryExpression) !QBEOperand {
    const alloc = ctx.arena.allocator();
    const idx = node.resolve().index;
    const rhs_lbl = try std.fmt.allocPrint(alloc, "or_rhs_{d}", .{idx});
    const end_lbl = try std.fmt.allocPrint(alloc, "or_end_{d}", .{idx});

    const result = try ctx.function().addBinding("__or_tmp", type_mod.the().boolean);
    try ctx.addOperation(.{ .store = .{ .value = ILValue.makeInteger(1, .{ .base = .B }), .pointer = result }});

    var lhs_op = try generateNode(ctx, p, be.lhs);
    try lhs_op.dereference(ctx);
    const lv = lhs_op.value orelse ILValue.makeInteger(0, .{ .base = .W });
    try ctx.addOperation(.{ .jnz = .{ .condition = lv, .on_true = end_lbl, .on_false = rhs_lbl }});
    try ctx.addOperation(.{ .label = .{ .name = rhs_lbl }});
    var rhs_op = try generateNode(ctx, p, be.rhs);
    try rhs_op.dereference(ctx);
    const rv = rhs_op.value orelse ILValue.makeInteger(0, .{ .base = .W });
    const rv_b = ctx.newLocal(.{ .base = .B });
    try ctx.addOperation(.{ .expr = .{ .target = rv_b, .lhs = rv, .rhs = ILValue.makeInteger(0, .{ .base = .W }), .op = .cmp_ne }});
    try ctx.addOperation(.{ .store = .{ .value = rv_b, .pointer = result }});
    try ctx.addOperation(.{ .jmp = .{ .label = end_lbl }});
    try ctx.addOperation(.{ .label = .{ .name = end_lbl }});
    return .{ .node = node, .value_type = type_mod.the().boolean, .value = result };
}

fn genUnaryExpression(ctx: *QBEContext, p: *Parser, node: AstNode, ue: sn.UnaryExpression) !QBEOperand {
    const th = node.getConst().bound_type;

    switch (ue.op) {
        .address_of => {
            // Return the operand as a pointer (no dereference)
            const op = try generateNode(ctx, p, ue.operand);
            return .{ .node = node, .value_type = th, .value = op.value };
        },
        .negate => {
            var op = try generateNode(ctx, p, ue.operand);
            try op.dereference(ctx);
            const il = qbeTypeCode(th);
            const result = ctx.newLocal(il);
            const zero: ILValue = switch (il.asBase()) {
                .S => ILValue.makeFloat(0.0, il),
                .D => ILValue.makeFloat(0.0, il),
                else => ILValue.makeInteger(0, il),
            };
            try ctx.addOperation(.{ .expr = .{ .target = result, .lhs = zero, .rhs = op.value orelse zero, .op = .sub }});
            return .{ .node = node, .value_type = th, .value = result };
        },
        .logical_invert => {
            var op = try generateNode(ctx, p, ue.operand);
            try op.dereference(ctx);
            const result = ctx.newLocal(.{ .base = .W });
            const one = ILValue.makeInteger(1, .{ .base = .W });
            try ctx.addOperation(.{ .expr = .{ .target = result, .lhs = op.value orelse one, .rhs = one, .op = .bit_xor }});
            const masked = ctx.newLocal(.{ .base = .W });
            try ctx.addOperation(.{ .expr = .{ .target = masked, .lhs = result, .rhs = one, .op = .bit_and }});
            return .{ .node = node, .value_type = th, .value = masked };
        },
        .binary_invert => {
            var op = try generateNode(ctx, p, ue.operand);
            try op.dereference(ctx);
            const il = qbeTypeCode(th);
            const all_ones = ILValue.makeInteger(-1, il);
            const result = ctx.newLocal(il);
            try ctx.addOperation(.{ .expr = .{ .target = result, .lhs = op.value orelse all_ones, .rhs = all_ones, .op = .bit_xor }});
            return .{ .node = node, .value_type = th, .value = result };
        },
        .unwrap => {
            // Optional/Result unwrap: call the runtime must() helper and load payload
            var op = try generateNode(ctx, p, ue.operand);
            const op_th = op.value_type;
            const op_t  = op_th.getConst();
            switch (op_t.description) {
                .optional_type => |ot| {
                    try op.materialize(ctx);
                    const ptr = op.value orelse return voidOp(node);
                    try ctx.addOperation(.{ .call = .{
                        .target = ILValue.makeNone(), .ret_type = .{ .base = .V },
                        .func_name = "cathode$optional_must", .full_name = "cathode$optional_must",
                        .args = &.{ .{ .il_type = .{ .base = .L }, .value = ptr } },
                    }});
                    const load_il = qbeLoadCode(ot.type);
                    const result_il = qbeTypeCode(ot.type);
                    const result = ctx.newLocal(result_il);
                    try ctx.addOperation(.{ .load = .{ .target = result, .pointer = ptr, .load_type = load_il }});
                    return .{ .node = node, .value_type = th, .value = result };
                },
                .result_type => |rt| {
                    try op.materialize(ctx);
                    const ptr = op.value orelse return voidOp(node);
                    try ctx.addOperation(.{ .call = .{
                        .target = ILValue.makeNone(), .ret_type = .{ .base = .V },
                        .func_name = "cathode$result_must", .full_name = "cathode$result_must",
                        .args = &.{ .{ .il_type = .{ .base = .L }, .value = ptr } },
                    }});
                    const result_il = qbeTypeCode(rt.success);
                    const result = ctx.newLocal(result_il);
                    try ctx.addOperation(.{ .load = .{ .target = result, .pointer = ptr, .load_type = qbeLoadCode(rt.success) }});
                    return .{ .node = node, .value_type = th, .value = result };
                },
                else => {},
            }
            return voidOp(node);
        },
        .sizeof => {
            const operand_th = ue.operand.getConst().bound_type;
            const sz: i64 = @intCast(operand_th.getConst().size_of());
            return .{ .node = node, .value_type = th, .value = ILValue.makeInteger(sz, qbeTypeCode(th)) };
        },
        .idempotent => return generateNode(ctx, p, ue.operand),
        else => return voidOp(node),
    }
}

// ── Operator mapping helpers ───────────────────────────────────────────────────

fn operatorToIL(op: Operator, lhs_th: TypeHandle) ?ILOperation {
    const t = lhs_th.getConst();
    const is_signed = switch (t.description) {
        .int_type => |it| it.is_signed,
        .float_type => true,
        else => true,
    };
    const is_float = t.description == .float_type;

    return switch (op) {
        .add           => .add,
        .subtract      => .sub,
        .multiply      => .mul,
        .divide        => if (is_signed) .div else .udiv,
        .modulo        => if (is_signed) .rem else .urem,
        .binary_and    => .bit_and,
        .binary_or     => .bit_or,
        .binary_xor    => .bit_xor,
        .shift_left    => .shl,
        .shift_right   => .sar,
        .equals        => .cmp_eq,
        .not_equal     => .cmp_ne,
        .less          => if (is_float) .cmp_flt else if (is_signed) .cmp_slt else .cmp_ult,
        .less_equal    => if (is_float) .cmp_fle else if (is_signed) .cmp_sle else .cmp_ule,
        .greater       => if (is_float) .cmp_fgt else if (is_signed) .cmp_sgt else .cmp_ugt,
        .greater_equal => if (is_float) .cmp_fge else if (is_signed) .cmp_sge else .cmp_uge,
        else => null,
    };
}

fn compoundAssignOp(op: Operator) ?ILOperation {
    return switch (op) {
        .assign_increment => .add,
        .assign_decrement => .sub,
        .assign_multiply  => .mul,
        .assign_divide    => .div,
        .assign_modulo    => .rem,
        .assign_and       => .bit_and,
        .assign_or        => .bit_or,
        .assign_xor       => .bit_xor,
        .assign_shift_left  => .shl,
        .assign_shift_right => .sar,
        else => null,
    };
}

// ── compileQbe — top-level entry point ───────────────────────────────────────

pub const CompileError = error{ QbeError, IoError, OutOfMemory };

pub fn compileQbe(
    gpa: Allocator,
    io: std.Io,
    p: *Parser,
    program_node: AstNode,
    exe_name: []const u8,
    lia_dir: []const u8,     // path to lib dir containing libcathodert.a
) !void {
    var ctx = try QBEContext.init(gpa, "program");
    defer ctx.deinit();

    // Generate IL
    _ = try generateNode(&ctx, p, program_node);

    // Create .cathode/ output dir
    std.Io.Dir.cwd().createDir(io, ".cathode", .default_dir) catch |err| {
        if (err != error.PathAlreadyExists) return err;
    };

    var obj_files = ArrayList([]const u8).init(gpa);
    defer {
        for (obj_files.items) |f| gpa.free(f);
        obj_files.deinit();
    }

    for (ctx.program.files.items) |*f| {
        // Derive base name from file path
        const base = baseName(f.name);
        const ssa_path = try std.fmt.allocPrint(gpa, ".cathode/{s}.ssa", .{base});
        defer gpa.free(ssa_path);
        const s_path   = try std.fmt.allocPrint(gpa, ".cathode/{s}.s",   .{base});
        defer gpa.free(s_path);
        const o_path   = try std.fmt.allocPrint(gpa, ".cathode/{s}.o",   .{base});
        const o_path_copy = try gpa.dupe(u8, o_path);
        defer gpa.free(o_path);
        try obj_files.append(o_path_copy);

        // Write .ssa
        {
            var buf: std.ArrayList(u8) = .empty;
            var aw = std.Io.Writer.Allocating.fromArrayList(gpa, &buf);
            try f.write(&aw.writer);
            try aw.writer.flush();
            var content = aw.toArrayList();
            defer content.deinit(gpa);
            const file_out = try std.Io.Dir.cwd().createFile(io, ssa_path, .{});
            defer file_out.close(io);
            try std.Io.File.writeStreamingAll(file_out, io, content.items);
        }

        // Run: qbe -o <s_path> <ssa_path>
        try runCmd(io, &.{ "qbe", "-o", s_path, ssa_path });

        // Run: as -o <o_path> <s_path>
        try runCmd(io, &.{ "as", "-o", o_path, s_path });
    }

    // Link: cc -o <exe_name> <obj...> -L<lia_dir>/lib -lcathodert -lm -lpthread -ldl
    var link_args = ArrayList([]const u8).init(gpa);
    defer link_args.deinit();
    try link_args.append("cc");
    try link_args.append("-o");
    try link_args.append(exe_name);
    for (obj_files.items) |obj| try link_args.append(obj);
    const lib_flag = try std.fmt.allocPrint(gpa, "-L{s}/lib", .{lia_dir});
    defer gpa.free(lib_flag);
    try link_args.append(lib_flag);
    try link_args.append("-lcathodert");
    // Extra libraries from extern declarations
    for (ctx.libraries.items) |lib| {
        const flag = try std.fmt.allocPrint(gpa, "-l{s}", .{lib});
        defer gpa.free(flag);
        try link_args.append(flag);
    }
    try link_args.append("-lm");
    try link_args.append("-lpthread");
    try link_args.append("-ldl");
    try runCmd(io, link_args.items);
}

fn baseName(path: []const u8) []const u8 {
    const last_slash = std.mem.lastIndexOfScalar(u8, path, '/') orelse return path;
    const name = path[last_slash + 1 ..];
    const last_dot = std.mem.lastIndexOfScalar(u8, name, '.') orelse return name;
    return name[0..last_dot];
}

fn runCmd(io: std.Io, argv: []const []const u8) !void {
    var child = try std.process.spawn(io, .{
        .argv = argv,
        .stdin = .ignore,
        .stdout = .inherit,
        .stderr = .inherit,
    });
    defer child.kill(io);
    const term = try child.wait(io);
    switch (term) {
        .exited => |code| if (code != 0) return error.QbeError,
        else => return error.QbeError,
    }
}
