// replaces Lang/Operator.h
const type_mod = @import("type.zig");

pub const Position = enum { prefix, infix, postfix, closing };
pub const Associativity = enum { left, right };

pub const Operator = enum {
    // Assignment operators
    assign,
    assign_and,
    assign_decrement,
    assign_divide,
    assign_increment,
    assign_modulo,
    assign_multiply,
    assign_or,
    assign_shift_left,
    assign_shift_right,
    assign_xor,

    // Binary / unary operators
    add,
    address_of,
    binary_and,
    binary_invert,
    binary_or,
    binary_xor,
    call,
    cast,
    divide,
    equals,
    greater,
    greater_equal,
    idempotent,
    length,
    less,
    less_equal,
    logical_and,
    logical_invert,
    logical_or,
    member_access,
    modulo,
    multiply,
    negate,
    not_equal,
    range,
    sequence,
    shift_left,
    shift_right,
    sizeof,
    subscript,
    subtract,
    unwrap,
    unwrap_error,
};

pub const PseudoType = enum {
    self,
    lhs,
    rhs,
    refer,
    @"error",
    boolean,
    byte,
    long,
    string,
};

// replaces C++ `using OperandType = std::variant<TypeKind, PseudoType>`
pub const TypeKind = type_mod.TypeKind;
pub const OperandType = union(enum) {
    type_kind: TypeKind,
    pseudo_type: PseudoType,
};

pub fn name(op: Operator) []const u8 {
    return switch (op) {
        .assign          => "=",
        .assign_and      => "&=",
        .assign_decrement => "-=",
        .assign_divide   => "/=",
        .assign_increment => "+=",
        .assign_modulo   => "%=",
        .assign_multiply => "*=",
        .assign_or       => "|=",
        .assign_shift_left  => "<<=",
        .assign_shift_right => ">>=",
        .assign_xor      => "^=",
        .add             => "+",
        .address_of      => "&",
        .binary_and      => "&",
        .binary_invert   => "~",
        .binary_or       => "|",
        .binary_xor      => "^",
        .call            => "()",
        .cast            => "::",
        .divide          => "/",
        .equals          => "==",
        .greater         => ">",
        .greater_equal   => ">=",
        .idempotent      => "+",
        .length          => "#",
        .less            => "<",
        .less_equal      => "<=",
        .logical_and     => "&&",
        .logical_invert  => "!",
        .logical_or      => "||",
        .member_access   => ".",
        .modulo          => "%",
        .multiply        => "*",
        .negate          => "-",
        .not_equal       => "!=",
        .range           => "..",
        .sequence        => ",",
        .shift_left      => "<<",
        .shift_right     => ">>",
        .sizeof          => "#::",
        .subscript       => "[]",
        .subtract        => "-",
        .unwrap          => "?",
        .unwrap_error    => "!",
    };
}
