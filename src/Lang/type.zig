// replaces Lang/Type.h + Lang/Type.cpp
const std = @import("std");
const ArrayList = std.array_list.Managed;
const Handle = @import("../Util/handle.zig").Handle;

pub const TypeHandle = Handle(Type);

// Sizes of C runtime structs (from rt/cathode.h) used for size_of/align_of.
// These are for 64-bit targets; cathode currently only supports 64-bit.
const slice_size: usize = 16;   // slice_t:  { void*(8), int64_t(8) }
const slice_align: usize = 8;
const dynarr_size: usize = 24;  // dynarr_t: { void*(8), int64_t(8), int64_t(8) }
const dynarr_align: usize = 8;
const ptr_size: usize = 8;
const ptr_align: usize = 8;

fn alignAt(bytes: usize, alignment: usize) usize {
    if (alignment == 0) return bytes;
    return (bytes + alignment - 1) & ~(alignment - 1);
}

// ── Primitive type descriptions ───────────────────────────────────────────────

pub const VoidType = struct {
    pub fn size_of(_: VoidType) usize { return 0; }
    pub fn align_of(_: VoidType) usize { return 0; }
};

pub const PointerType = struct {
    referencing: TypeHandle,
    pub fn size_of(_: PointerType) usize { return ptr_size; }
    pub fn align_of(_: PointerType) usize { return ptr_align; }
};

pub const ModuleType = struct {
    pub fn size_of(_: ModuleType) usize { return 0; }
    pub fn align_of(_: ModuleType) usize { return 0; }
};

pub const FunctionType = struct {
    parameters: TypeHandle, // always a TypeList
    result: TypeHandle,
    pub fn size_of(_: FunctionType) usize { return ptr_size; }
    pub fn align_of(_: FunctionType) usize { return ptr_align; }
};

pub const TypeList = struct {
    types: []TypeHandle,
    pub fn size_of(self: TypeList) usize {
        var ret: usize = 0;
        for (self.types) |t| {
            ret = alignAt(ret, t.getConst().align_of()) + t.getConst().size_of();
        }
        return ret;
    }
    pub fn align_of(self: TypeList) usize {
        var ret: usize = 0;
        for (self.types) |t| ret = @max(ret, t.getConst().align_of());
        return ret;
    }
};

pub const GenericParameter = struct {
    name: []const u8,
    pub fn size_of(_: GenericParameter) usize { return 0; }
    pub fn align_of(_: GenericParameter) usize { return 0; }
};

pub const IntType = struct {
    is_signed: bool,
    width_bits: u8,
    pub fn size_of(self: IntType) usize { return self.width_bits / 8; }
    pub fn align_of(self: IntType) usize { return self.width_bits / 8; }
};

pub const FloatType = struct {
    width_bits: u8,
    pub fn size_of(self: FloatType) usize { return self.width_bits / 8; }
    pub fn align_of(self: FloatType) usize { return self.width_bits / 8; }
};

pub const BoolType = struct {
    pub fn size_of(_: BoolType) usize { return 1; }
    pub fn align_of(_: BoolType) usize { return 1; }
};

pub const ReferenceType = struct {
    referencing: TypeHandle,
    pub fn size_of(_: ReferenceType) usize { return ptr_size; }
    pub fn align_of(_: ReferenceType) usize { return ptr_align; }
};

pub const SliceType = struct {
    slice_of: TypeHandle,
    pub fn size_of(_: SliceType) usize { return slice_size; }
    pub fn align_of(_: SliceType) usize { return slice_align; }
};

pub const ZeroTerminatedArray = struct {
    array_of: TypeHandle,
    pub fn size_of(_: ZeroTerminatedArray) usize { return ptr_size; }
    pub fn align_of(_: ZeroTerminatedArray) usize { return ptr_align; }
};

pub const Array = struct {
    array_of: TypeHandle,
    size: usize,
    pub fn size_of(self: Array) usize { return self.size * self.array_of.getConst().size_of(); }
    pub fn align_of(self: Array) usize { return self.array_of.getConst().align_of(); }
};

pub const DynArray = struct {
    array_of: TypeHandle,
    pub fn size_of(_: DynArray) usize { return dynarr_size; }
    pub fn align_of(_: DynArray) usize { return dynarr_align; }
};

pub const RangeType = struct {
    range_of: TypeHandle,
    pub fn size_of(self: RangeType) usize { return 3 * self.range_of.getConst().size_of(); }
    pub fn align_of(self: RangeType) usize { return self.range_of.getConst().align_of(); }
};

// ── Compound type descriptions ────────────────────────────────────────────────

pub const EnumValue = struct {
    label: []const u8,
    value: i64,
};

pub const EnumType = struct {
    underlying_type: TypeHandle,
    values: []const EnumValue,

    pub fn size_of(self: EnumType) usize { return self.underlying_type.getConst().size_of(); }
    pub fn align_of(self: EnumType) usize { return self.underlying_type.getConst().align_of(); }

    pub fn isValid(self: EnumType, label: []const u8) bool {
        for (self.values) |v| if (std.mem.eql(u8, v.label, label)) return true;
        return false;
    }

    pub fn valueFor(self: EnumType, label: []const u8) ?i64 {
        for (self.values) |v| if (std.mem.eql(u8, v.label, label)) return v.value;
        return null;
    }
};

pub const UnionTag = struct {
    value: i64,
    payload: TypeHandle, // null_handle means void payload
};

pub const TaggedUnionType = struct {
    tag_type: TypeHandle, // always an EnumType
    tags: []const UnionTag,

    pub fn align_of(self: TaggedUnionType) usize {
        var ret = self.tag_type.getConst().align_of();
        for (self.tags) |tag| {
            if (!tag.payload.isNull()) ret = @max(ret, tag.payload.getConst().align_of());
        }
        return ret;
    }

    pub fn size_of(self: TaggedUnionType) usize {
        var max: usize = 0;
        for (self.tags) |tag| {
            if (!tag.payload.isNull()) {
                max = @max(max, alignAt(tag.payload.getConst().size_of(), self.align_of()));
            }
        }
        max = alignAt(max, self.tag_type.getConst().align_of());
        return max + self.tag_type.getConst().size_of();
    }

    pub fn tagOffset(self: TaggedUnionType) usize {
        var max: usize = 0;
        for (self.tags) |tag| {
            if (!tag.payload.isNull()) max = @max(max, tag.payload.getConst().size_of());
        }
        return alignAt(max, self.tag_type.getConst().align_of());
    }

    pub fn isValid(self: TaggedUnionType, label: []const u8) bool {
        return self.tag_type.getConst().description.enum_type.isValid(label);
    }

    pub fn valueFor(self: TaggedUnionType, label: []const u8) ?i64 {
        return self.tag_type.getConst().description.enum_type.valueFor(label);
    }

    pub fn payloadFor(self: TaggedUnionType, tag_value: i64) TypeHandle {
        for (self.tags) |tag| {
            if (tag.value == tag_value) return tag.payload;
        }
        return the().void_type;
    }
};

pub const OptionalType = struct {
    type: TypeHandle,
    // C++: TypeRegistry::boolean->size_of() + type->size_of()
    pub fn size_of(self: OptionalType) usize { return 1 + self.type.getConst().size_of(); }
    pub fn align_of(self: OptionalType) usize { return self.type.getConst().align_of(); }
};

pub const ResultType = struct {
    success: TypeHandle,
    error_type: TypeHandle, // renamed from 'error' (reserved keyword in Zig)

    pub fn flag_offset(self: ResultType) usize {
        const a = @max(self.success.getConst().align_of(), self.error_type.getConst().align_of());
        return alignAt(@max(self.success.getConst().size_of(), self.error_type.getConst().size_of()), a);
    }

    pub fn size_of(self: ResultType) usize { return self.flag_offset() + 1; }
    pub fn align_of(self: ResultType) usize {
        return @max(self.success.getConst().align_of(), self.error_type.getConst().align_of());
    }
};

pub const StructField = struct {
    name: []const u8,
    type: TypeHandle,
};

pub const StructType = struct {
    fields: []const StructField,

    pub fn size_of(self: StructType) usize {
        var s: usize = 0;
        for (self.fields) |f| {
            s = alignAt(s, f.type.getConst().align_of()) + f.type.getConst().size_of();
        }
        return s;
    }

    pub fn align_of(self: StructType) usize {
        var ret: usize = 0;
        for (self.fields) |f| ret = @max(ret, f.type.getConst().align_of());
        return ret;
    }

    pub fn offsetOf(self: StructType, field_name: []const u8) ?usize {
        var offset: usize = 0;
        for (self.fields) |f| {
            if (std.mem.eql(u8, f.name, field_name)) return offset;
            offset = alignAt(offset, f.type.getConst().align_of()) + f.type.getConst().size_of();
        }
        return null;
    }
};

pub const TypeAlias = struct {
    alias_of: TypeHandle,
    pub fn size_of(self: TypeAlias) usize { return self.alias_of.getConst().size_of(); }
    pub fn align_of(self: TypeAlias) usize { return self.alias_of.getConst().align_of(); }
};

pub const TypeType = struct {
    type: TypeHandle,
    pub fn size_of(_: TypeType) usize { return 0; }
    pub fn align_of(_: TypeType) usize { return 0; }
};

// ── TypeDescription: discriminated union, same order as C++ TypeKind enum ─────

pub const TypeDescription = union(enum) {
    void_type: VoidType,
    pointer_type: PointerType,
    module_type: ModuleType,
    function_type: FunctionType,
    type_list: TypeList,
    generic_parameter: GenericParameter,
    int_type: IntType,
    float_type: FloatType,
    bool_type: BoolType,
    reference_type: ReferenceType,
    slice_type: SliceType,
    zero_terminated_array: ZeroTerminatedArray,
    array: Array,
    dyn_array: DynArray,
    range_type: RangeType,
    enum_type: EnumType,
    tagged_union_type: TaggedUnionType,
    optional_type: OptionalType,
    result_type: ResultType,
    struct_type: StructType,
    type_alias: TypeAlias,
    type_type: TypeType,
};

// Mirrors C++ TypeKind enum; the integer values must match the union tag order above.
pub const TypeKind = std.meta.Tag(TypeDescription);

// ── Type: a named type with its description ───────────────────────────────────

pub const Type = struct {
    id: TypeHandle = TypeHandle.null_handle,
    name: []const u8,
    description: TypeDescription,

    pub fn is_a(self: *const Type, k: TypeKind) bool {
        return std.meta.activeTag(self.description) == k;
    }

    pub fn kind(self: *const Type) TypeKind {
        return std.meta.activeTag(self.description);
    }

    pub fn size_of(self: *const Type) usize {
        return switch (self.description) {
            inline else => |d| d.size_of(),
        };
    }

    pub fn align_of(self: *const Type) usize {
        return switch (self.description) {
            inline else => |d| d.align_of(),
        };
    }

    // Unwrap one level of ReferenceType; returns self for all other types.
    pub fn value_type(self: *const Type) TypeHandle {
        return switch (self.description) {
            .reference_type => |r| r.referencing,
            else => self.id,
        };
    }

    // Structural compatibility: can self supply a value where `other` is expected?
    pub fn compatible(self: *const Type, other: TypeHandle) bool {
        if (TypeHandle.eql(self.id, other)) return true;

        if (self.is_a(.reference_type)) {
            return self.description.reference_type.referencing.getConst().compatible(other);
        }
        if (other.getConst().is_a(.reference_type)) {
            return self.compatible(other.getConst().description.reference_type.referencing);
        }

        // Order (self, other) by TypeKind index so left <= right.
        const si = @intFromEnum(std.meta.activeTag(self.description));
        const oi = @intFromEnum(std.meta.activeTag(other.getConst().description));
        const left  = if (si <= oi) self.id else other;
        const right = if (si <= oi) other else self.id;
        const lt = left.getConst();
        const rt = right.getConst();

        // The only compatible cross-kind pairs involve TypeList on the left.
        switch (lt.description) {
            .type_list => |list| switch (rt.description) {
                .slice_type => |sl| {
                    for (list.types) |t| if (!t.getConst().compatible(sl.slice_of)) return false;
                    return true;
                },
                .dyn_array => |da| {
                    for (list.types) |t| if (!t.getConst().compatible(da.array_of)) return false;
                    return true;
                },
                .array => |arr| {
                    if (list.types.len != arr.size) return false;
                    for (list.types) |t| if (!t.getConst().compatible(arr.array_of)) return false;
                    return true;
                },
                .zero_terminated_array => |za| {
                    for (list.types) |t| if (!t.getConst().compatible(za.array_of)) return false;
                    return true;
                },
                .struct_type => |s| {
                    if (list.types.len != s.fields.len) return false;
                    for (list.types, s.fields) |lt2, rf| {
                        if (!lt2.getConst().compatible(rf.type)) return false;
                    }
                    return true;
                },
                else => return false,
            },
            else => return false,
        }
    }

    // Whether a value of `self` type can be assigned to a variable of type `lhs`.
    pub fn assignable_to(self: *const Type, lhs: TypeHandle) bool {
        if (TypeHandle.eql(self.id, lhs)) return true;
        const rhs = self.id;
        const lhs_t = lhs.getConst();
        switch (lhs_t.description) {
            .optional_type => |opt| {
                if (self.is_a(.void_type)) return true;         // Optional accepts Void (null)
                return TypeHandle.eql(opt.type, rhs);           // Optional accepts its inner type
            },
            .bool_type => {
                // Bool can receive the has-value flag from Optional or Result
                if (self.is_a(.optional_type) or self.is_a(.result_type)) return true;
                return false;
            },
            .result_type => |res| {
                return TypeHandle.eql(res.success, rhs) or TypeHandle.eql(res.error_type, rhs);
            },
            .reference_type => |ref| {
                // When assigning to a reference, delegate to the referenced type.
                return ref.referencing.getConst().assignable_to(lhs);
            },
            else => {
                if (self.is_a(.reference_type)) {
                    return self.description.reference_type.referencing.getConst().assignable_to(lhs);
                }
                return false;
            },
        }
    }
};

// ── TypeRegistry singleton ────────────────────────────────────────────────────

pub const TypeRegistry = struct {
    types: ArrayList(Type),
    arena: std.heap.ArenaAllocator,

    // Well-known type handles, set during init()
    u8_type: TypeHandle = TypeHandle.null_handle,
    u16_type: TypeHandle = TypeHandle.null_handle,
    u32_type: TypeHandle = TypeHandle.null_handle,
    u64_type: TypeHandle = TypeHandle.null_handle,
    i8_type: TypeHandle = TypeHandle.null_handle,
    i16_type: TypeHandle = TypeHandle.null_handle,
    i32_type: TypeHandle = TypeHandle.null_handle,
    i64_type: TypeHandle = TypeHandle.null_handle,
    f32_type: TypeHandle = TypeHandle.null_handle,
    f64_type: TypeHandle = TypeHandle.null_handle,
    boolean: TypeHandle = TypeHandle.null_handle,
    string: TypeHandle = TypeHandle.null_handle,
    string_builder: TypeHandle = TypeHandle.null_handle,
    cstring: TypeHandle = TypeHandle.null_handle,
    character: TypeHandle = TypeHandle.null_handle,
    void_type: TypeHandle = TypeHandle.null_handle,
    pointer: TypeHandle = TypeHandle.null_handle,
    module: TypeHandle = TypeHandle.null_handle,

    pub fn init(gpa: std.mem.Allocator) !TypeRegistry {
        var reg = TypeRegistry{
            .types = ArrayList(Type).init(gpa),
            .arena = std.heap.ArenaAllocator.init(gpa),
        };

        reg.u8_type  = try reg.makeType("u8",  .{ .int_type = .{ .is_signed = false, .width_bits = 8 } });
        reg.u16_type = try reg.makeType("u16", .{ .int_type = .{ .is_signed = false, .width_bits = 16 } });
        reg.u32_type = try reg.makeType("u32", .{ .int_type = .{ .is_signed = false, .width_bits = 32 } });
        reg.u64_type = try reg.makeType("u64", .{ .int_type = .{ .is_signed = false, .width_bits = 64 } });
        reg.i8_type  = try reg.makeType("i8",  .{ .int_type = .{ .is_signed = true,  .width_bits = 8 } });
        reg.i16_type = try reg.makeType("i16", .{ .int_type = .{ .is_signed = true,  .width_bits = 16 } });
        reg.i32_type = try reg.makeType("i32", .{ .int_type = .{ .is_signed = true,  .width_bits = 32 } });
        reg.i64_type = try reg.makeType("i64", .{ .int_type = .{ .is_signed = true,  .width_bits = 64 } });
        reg.f32_type = try reg.makeType("f32", .{ .float_type = .{ .width_bits = 32 } });
        reg.f64_type = try reg.makeType("f64", .{ .float_type = .{ .width_bits = 64 } });
        reg.boolean  = try reg.makeType("bool", .{ .bool_type = .{} });
        // string = []u32 (UTF-32 slice — the in-language string type)
        reg.string   = try reg.makeType("string", .{ .slice_type = .{ .slice_of = reg.u32_type } });
        // string_builder = [*]u32 (dynamic array of codepoints)
        reg.string_builder = try reg.makeType("string_builder", .{ .dyn_array = .{ .array_of = reg.u32_type } });
        // cstring = [0]u8 (zero-terminated byte array)
        reg.cstring  = try reg.makeType("cstring", .{ .zero_terminated_array = .{ .array_of = reg.u8_type } });
        // char = alias of u32 (Unicode codepoint)
        reg.character = try reg.makeType("char", .{ .type_alias = .{ .alias_of = reg.u32_type } });
        reg.void_type = try reg.makeType("void", .{ .void_type = .{} });
        reg.pointer   = try reg.makeType("pointer", .{ .pointer_type = .{ .referencing = reg.void_type } });
        reg.module    = try reg.makeType("module", .{ .module_type = .{} });

        return reg;
    }

    pub fn deinit(self: *TypeRegistry) void {
        self.types.deinit();
        self.arena.deinit();
    }

    // ── Factory methods ───────────────────────────────────────────────────────

    pub fn genericParameter(self: *TypeRegistry, name: []const u8) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .generic_parameter and
                std.mem.eql(u8, t.description.generic_parameter.name, name))
            {
                return t.id;
            }
        }
        const duped = try self.arena.allocator().dupe(u8, name);
        return self.makeType(duped, .{ .generic_parameter = .{ .name = duped } });
    }

    pub fn referencing(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .reference_type and
                TypeHandle.eql(t.description.reference_type.referencing, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "&{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .reference_type = .{ .referencing = typ } });
    }

    pub fn pointerTo(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .pointer_type and
                TypeHandle.eql(t.description.pointer_type.referencing, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "*{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .pointer_type = .{ .referencing = typ } });
    }

    pub fn aliasFor(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        const name = try std.fmt.allocPrint(self.arena.allocator(), "AliasOf({s})", .{typ.getConst().name});
        return self.makeType(name, .{ .type_alias = .{ .alias_of = typ } });
    }

    pub fn sliceOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .slice_type and
                TypeHandle.eql(t.description.slice_type.slice_of, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "[]{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .slice_type = .{ .slice_of = typ } });
    }

    pub fn zeroTerminatedArrayOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .zero_terminated_array and
                TypeHandle.eql(t.description.zero_terminated_array.array_of, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "[0]{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .zero_terminated_array = .{ .array_of = typ } });
    }

    pub fn arrayOf(self: *TypeRegistry, typ: TypeHandle, size: usize) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .array and
                TypeHandle.eql(t.description.array.array_of, typ) and
                t.description.array.size == size)
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "[{d}]{s}", .{ size, typ.getConst().name });
        return self.makeType(name, .{ .array = .{ .array_of = typ, .size = size } });
    }

    pub fn dynArrayOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .dyn_array and
                TypeHandle.eql(t.description.dyn_array.array_of, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "[*]{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .dyn_array = .{ .array_of = typ } });
    }

    pub fn optionalOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .optional_type and
                TypeHandle.eql(t.description.optional_type.type, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "?{s}", .{typ.getConst().name});
        return self.makeType(name, .{ .optional_type = .{ .type = typ } });
    }

    pub fn rangeOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .range_type and
                TypeHandle.eql(t.description.range_type.range_of, typ))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "{s}..", .{typ.getConst().name});
        return self.makeType(name, .{ .range_type = .{ .range_of = typ } });
    }

    pub fn resultOf(self: *TypeRegistry, success: TypeHandle, error_type: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .result_type and
                TypeHandle.eql(t.description.result_type.success, success) and
                TypeHandle.eql(t.description.result_type.error_type, error_type))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(
            self.arena.allocator(), "{s}/{s}", .{ success.getConst().name, error_type.getConst().name });
        return self.makeType(name, .{ .result_type = .{ .success = success, .error_type = error_type } });
    }

    pub fn typeListOf(self: *TypeRegistry, type_list: []const TypeHandle) !TypeHandle {
        outer: for (self.types.items) |t| {
            if (t.description != .type_list) continue;
            const existing = t.description.type_list.types;
            if (existing.len != type_list.len) continue;
            for (existing, type_list) |a, b| {
                if (!TypeHandle.eql(a, b)) continue :outer;
            }
            return t.id;
        }
        const duped = try self.arena.allocator().dupe(TypeHandle, type_list);
        // Build name like "(u8,u16,...)"
        var name_buf = ArrayList(u8).init(self.arena.allocator());
        try name_buf.append('(');
        for (type_list, 0..) |t, i| {
            if (i > 0) try name_buf.appendSlice(",");
            try name_buf.appendSlice(t.getConst().name);
        }
        try name_buf.append(')');
        return self.makeType(name_buf.items, .{ .type_list = .{ .types = duped } });
    }

    pub fn functionOf(self: *TypeRegistry, params: []const TypeHandle, result: TypeHandle) !TypeHandle {
        const params_type = try self.typeListOf(params);
        for (self.types.items) |t| {
            if (t.description == .function_type and
                TypeHandle.eql(t.description.function_type.parameters, params_type) and
                TypeHandle.eql(t.description.function_type.result, result))
            {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(
            self.arena.allocator(), "func{s} {s}",
            .{ params_type.getConst().name, result.getConst().name });
        return self.makeType(name, .{ .function_type = .{ .parameters = params_type, .result = result } });
    }

    pub fn structOf(self: *TypeRegistry, fields: []const StructField) !TypeHandle {
        outer: for (self.types.items) |t| {
            if (t.description != .struct_type) continue;
            const existing = t.description.struct_type.fields;
            if (existing.len != fields.len) continue;
            for (existing, fields) |ef, nf| {
                if (!std.mem.eql(u8, ef.name, nf.name) or !TypeHandle.eql(ef.type, nf.type)) continue :outer;
            }
            return t.id;
        }
        const duped = try self.arena.allocator().dupe(StructField, fields);
        var name_buf = ArrayList(u8).init(self.arena.allocator());
        try name_buf.append('{');
        for (fields, 0..) |f, i| {
            if (i > 0) try name_buf.appendSlice(",");
            try name_buf.appendSlice(f.name);
            try name_buf.appendSlice(": ");
            try name_buf.appendSlice(f.type.getConst().name);
        }
        try name_buf.append('}');
        return self.makeType(name_buf.items, .{ .struct_type = .{ .fields = duped } });
    }

    pub fn typeOf(self: *TypeRegistry, typ: TypeHandle) !TypeHandle {
        for (self.types.items) |t| {
            if (t.description == .type_type and TypeHandle.eql(t.description.type_type.type, typ)) {
                return t.id;
            }
        }
        const name = try std.fmt.allocPrint(self.arena.allocator(), "meta({s})", .{typ.getConst().name});
        return self.makeType(name, .{ .type_type = .{ .type = typ } });
    }

    // ── Public named-type factory ─────────────────────────────────────────────
    // Used by ast.zig when binding Enum, Struct, Alias nodes.
    pub fn namedType(self: *TypeRegistry, name: []const u8, description: TypeDescription) !TypeHandle {
        return self.makeType(name, description);
    }

    // ── Internal helpers ──────────────────────────────────────────────────────

    fn makeType(self: *TypeRegistry, name: []const u8, description: TypeDescription) !TypeHandle {
        const handle = try TypeHandle.append(&self.types, .{
            .name = name,
            .description = description,
        });
        handle.get().id = handle;
        return handle;
    }
};

// ── Module-level singleton ────────────────────────────────────────────────────

var registry_instance: TypeRegistry = undefined;
var registry_ready: bool = false;

// Initialize the global TypeRegistry.  Must be called once before any type operations.
pub fn init(gpa: std.mem.Allocator) !void {
    registry_instance = try TypeRegistry.init(gpa);
    registry_ready = true;
}

pub fn deinit() void {
    registry_instance.deinit();
    registry_ready = false;
}

// Returns a pointer to the global TypeRegistry.
pub fn the() *TypeRegistry {
    std.debug.assert(registry_ready);
    return &registry_instance;
}
