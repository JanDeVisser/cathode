/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <ranges>

#include <Util/Align.h>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

namespace Lia::QBE {

using namespace Util;
using namespace Lia;

std::wostream &operator<<(std::wostream &os, QBEValue const &value)
{
    std::visit(
        overloads {
            [&os](std::monostate const &) {
                os << L"(void)";
            },
            [&os](auto const &payload) {
                os << payload;
            } },
        value.payload);
    os << " " << value.type;
    return os;
}

QBEValue Frame::make_from_buffer(ILValue target, QBEValue val)
{
    assert(val.type == ILBaseType::L);
    void const *buffer = static_cast<void const *>(val);
    return std::visit(
        overloads {
            [this, &buffer](ILBaseType const &bt) -> QBEValue {
                switch (bt) {
                case ILBaseType::V:
                    return QBEValue {};
                case ILBaseType::B:
                case ILBaseType::SB:
                    return QBEValue { *((int8_t *) buffer) };
                case ILBaseType::UB:
                    return QBEValue { *((uint8_t *) buffer) };
                case ILBaseType::H:
                case ILBaseType::SH:
                    return QBEValue { *((int16_t *) buffer) };
                case ILBaseType::UH:
                    return QBEValue { *((uint16_t *) buffer) };
                case ILBaseType::W:
                case ILBaseType::SW:
                    return QBEValue { *((int32_t *) buffer) };
                case ILBaseType::UW:
                    return QBEValue { *((uint32_t *) buffer) };
                case ILBaseType::L:
                    return QBEValue { *((int64_t *) buffer) };
                case ILBaseType::S:
                    return QBEValue { *((float *) buffer) };
                case ILBaseType::D:
                    return QBEValue { *((double *) buffer) };
                }
                return QBEValue {};
            },
            [this, &val, &target](ILType::ILAggregate const &) -> QBEValue {
                return val;
            } },
        target.type.inner);
}

bool is_pointer(ILValue const &value)
{
    return std::visit(
        overloads {
            [](ILBaseType const &bt) -> bool {
                return bt == ILBaseType::L;
            },
            [](ILType::ILAggregate const &) -> bool {
                return true;
            } },
        value.type.inner);
}

QBEValue Frame::allocate(size_t bytes, size_t alignment)
{
    return vm.allocate(bytes, alignment);
}

QBEValue Frame::allocate(ILType const &type)
{
    return vm.allocate(type);
}

void Frame::release(QBEValue ptr)
{
    vm.release(ptr);
}

QBEValue VM::allocate(size_t bytes, size_t alignment)
{
    if (bytes == 0) {
        return QBEValue { stack.data() + stack_pointer };
    }
    if (stack_pointer + bytes > STACK_SIZE) {
        fatal("Stack overflow");
    }
    auto     ptr_base = alignat(stack_pointer, alignment);
    intptr_t ptr { reinterpret_cast<intptr_t>(&stack[ptr_base]) };
    stack_pointer = ptr_base + bytes;
    trace("Allocated {} bytes aligned at {}. New stack pointer {}", bytes, alignment, stack_pointer);
    return QBEValue { ptr };
}

QBEValue VM::allocate(ILType const &type)
{
    if (size_of(type) == 0) {
        return QBEValue { stack.data() + stack_pointer };
    }
    trace(L"Allocating size {}, align {}", size_of(type), align_of(type));
    return allocate(size_of(type), align_of(type));
}

void VM::release(QBEValue ptr)
{
    auto const *p = static_cast<uint8_t const *>(ptr);
    if (p < stack.data() || p > stack.data() + STACK_SIZE) {
        fatal("Can only release stack memory");
    }
    if (p > stack.data() + stack_pointer) {
        fatal("Releasing unallocated stack space");
    }
    auto bytes = (stack.data() + stack_pointer) - p;
    stack_pointer = p - stack.data();
    trace("Released {} bytes. New stack pointer {}", bytes, stack_pointer);
}

void assign(pFrame const &frame, ILValue const &val_ref, QBEValue const &v)
{
    if (size_of(v.type) == 0) {
        return;
    }
    std::visit(
        overloads {
            [frame, &v](ILValue::Local const &local) {
                trace(L"Set local {} = {}", local.var, v);
                if (frame->locals.size() < local.var + 1) {
                    frame->locals.resize(local.var + 1);
                }
                frame->locals[local.var] = v;
            },
            [frame, &v](ILValue::Variable const &variable) {
                frame->variables[variable.name()] = v;
            },
            [frame, &v](ILValue::ReturnValue const &ret_val) {
                frame->return_value = v;
            },
            [frame, &v](ILValue::Parameter const &parameter) {
                frame->variables[parameter.name()] = v;
            },
            [](auto const &inner) {
                fatal("Execute::assign({})", typeid(decltype(inner)).name());
            } },
        val_ref.inner);
}

QBEValue get(pFrame const &frame, ILValue const &val_ref)
{
    auto ret = std::visit(
        overloads {
            [&frame](ILValue::Local const &local) -> QBEValue {
                if (frame->locals.size() < local.var + 1) {
                    fatal("No local value with id `{}`in frame", local.var);
                }
                trace(L"Get local {} = {:x}", local.var, frame->locals[local.var]);
                return frame->locals[local.var];
            },
            [&frame](ILValue::Variable const &variable) -> QBEValue {
                if (frame->variables.contains(variable.name())) {
                    auto v = frame->variables[variable.name()];
                    trace(L"Get variable {} = {:x}", frame->variables[variable.name()], v);
                    return v;
                }
                fatal(L"No variable with name `{}` in frame", variable.name());
                return {};
            },
            [&frame](ILValue::Parameter const &param) -> QBEValue {
                if (frame->arguments.contains(param.name())) {
                    auto v = frame->arguments[param.name()];
                    return v;
                }
                fatal(L"No parameter with name `{}` in frame", param.name());
                return {};
            },
            [&frame](ILValue::Global const &global) -> QBEValue {
                auto const &globals = frame->vm.globals[frame->file.id];
                if (globals.contains(global.name)) {
                    trace(L"Get global {} = {:x}", global.name, globals.at(global.name));
                    return globals.at(global.name);
                }
                fatal(L"No global with name `{}`", global.name);
                return {};
            },
            [&frame](ILValue::ReturnValue const &) -> QBEValue {
                trace(L"get(ReturnValue) {}", frame->return_value);
                return frame->return_value;
            },
            [](int64_t const &int_val) -> QBEValue {
                QBEValue v { int_val };
                return v;
            },
            [](double const &dbl_val) -> QBEValue {
                return { dbl_val };
            },
            [](auto const &inner) -> QBEValue {
                fatal("Value get({})", typeid(decltype(inner)).name());
            } },
        val_ref.inner);
    return ret;
}

void store(pFrame const &frame, QBEValue target, ILValue const &src_ref)
{
    assert(target.type == ILBaseType::L);
    if (size_of(src_ref.type) == 0) {
        return;
    }
    QBEValue src = get(frame, src_ref);
    auto    *ptr = static_cast<uint8_t *>(target);

    auto store_value = [](QBEValue const &val, uint8_t *target_ptr) {
        uint8_t  p8;
        uint16_t p16;
        uint32_t p32;
        int64_t  p64;
        uint8_t *src_ptr;
        float    f32;
        double   f64;

        switch (val.type) {
        case ILBaseType::B:
        case ILBaseType::SB:
        case ILBaseType::UB:
            p8 = static_cast<uint8_t>(val);
            src_ptr = &p8;
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
        case ILBaseType::UH:
            p16 = static_cast<uint16_t>(val);
            src_ptr = reinterpret_cast<uint8_t *>(&p16);
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
        case ILBaseType::UW:
            p32 = static_cast<uint32_t>(val);
            src_ptr = reinterpret_cast<uint8_t *>(&p32);
            break;
        case ILBaseType::L:
            p64 = static_cast<int64_t>(val);
            src_ptr = reinterpret_cast<uint8_t *>(&p64);
            break;
        case ILBaseType::S:
            f32 = static_cast<float>(val);
            src_ptr = reinterpret_cast<uint8_t *>(&f32);
            break;
        case ILBaseType::D:
            f64 = static_cast<double>(val);
            src_ptr = reinterpret_cast<uint8_t *>(&f64);
            break;
        default:
            break;
        }
        memset(target_ptr, 0, size_of(val.type));
        memcpy(target_ptr, src_ptr, size_of(val.type));
    };

    auto dereference_value = [](uint8_t const *ptr, ILBaseType type) -> QBEValue {
        QBEValue ret;
        ret.type = type;
        switch (type) {
        case ILBaseType::B:
        case ILBaseType::SB:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<int8_t const *>(ptr)));
            break;
        case ILBaseType::UB:
            ret.payload = static_cast<intptr_t>(*ptr);
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<int16_t const *>(ptr)));
            break;
        case ILBaseType::UH:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<uint16_t const *>(ptr)));
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<int32_t const *>(ptr)));
            break;
        case ILBaseType::UW:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<uint32_t const *>(ptr)));
            break;
        case ILBaseType::L:
            ret.payload = static_cast<intptr_t>(*(reinterpret_cast<int64_t const *>(ptr)));
            break;
        case ILBaseType::S:
            ret.payload = static_cast<double>(*(reinterpret_cast<float const *>(ptr)));
            break;
        case ILBaseType::D:
            ret.payload = *(reinterpret_cast<double const *>(ptr));
            break;
        default:
            break;
        }
        return ret;
    };

    std::visit(
        overloads {
            [&store_value, &src, &ptr](ILBaseType const &bt) {
                store_value(src, ptr);
            },
            [&store_value, &dereference_value, &src, &ptr](ILType::ILAggregate const &aggregate) {
                auto *src_ptr = static_cast<uint8_t *>(src);
                memset(ptr, 0, aggregate.size_of);
                memcpy(ptr, src_ptr, aggregate.size_of);
            } },
        src_ref.type.inner);
}

VM::VM(ILProgram const &program)
    : program(program)
{
    globals.resize(program.files.size());
}

size_t VM::size() const
{
    return frames.size();
}

bool VM::empty() const
{
    return frames.empty();
}

pFrame VM::new_frame(ILFile const &file, ILFunction const &function)
{
    frames.emplace_back(*this, file, function);
    return { this };
}

void VM::release_frame()
{
    frames.pop_back();
}

Frame const &VM::operator[](size_t ix) const
{
    return frames[ix];
}

Frame &VM::operator[](size_t ix)
{
    return frames[ix];
}

void VM::dump_stack() const
{
    trace("  Stack SP = 0x{:04x} Base = 0x{:016x}", stack_pointer, reinterpret_cast<intptr_t>(stack.data()));
    for (auto ix = 0; ix < stack_pointer; ix += sizeof(uint64_t)) {
        trace("    0x{:02x}: 0x{:016x}", ix, *reinterpret_cast<uint64_t const *>(&stack[ix]));
    }
}

void Frame::dump_frame() const
{
    trace("  Variables:");
    for (auto const &[name, val] : variables) {
        trace(L"    {}: {:tx}", name, val);
    }
    trace("  Locals:");
    for (auto const &[ix, l] : std::ranges::views::enumerate(locals)) {
        if (ix > 0) {
            trace(L"    {}: {:tx}", ix, l);
        }
    }
    vm.dump_stack();
}

struct ExecError {
    std::wstring message;
};

using ExecTermination = std::variant<QBEValue, ExecError>;
using ExecResult = std::expected<ILValue, ExecTermination>;

template<typename InstrDef>
ExecResult execute(ILFunction const &il, pFrame const &frame, InstrDef const &instruction)
{
    NYI("Unimplemented execute() for {}", typeid(InstrDef).name());
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, AllocDef const &instruction)
{
    auto ptr { frame->allocate(instruction.bytes, instruction.alignment) };
    assign(frame, instruction.target, ptr);
    ++frame->ip;
    return instruction.target;
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, BlitDef const &instruction)
{
    assert(is_pointer(instruction.src));
    assert(is_pointer(instruction.dest));
    auto src = static_cast<uint8_t *>(get(frame, instruction.src));
    auto dest = static_cast<uint8_t *>(get(frame, instruction.dest));
    trace(L"Blit({} ({}), {} ({}), {})", src - frame->vm.stack.data(), instruction.src, dest - frame->vm.stack.data(), instruction.dest, instruction.bytes);
    for (auto ix = 0; ix < instruction.bytes; ++ix) {
        *(dest + ix) = *(src + ix);
    }
    ++frame->ip;
    return instruction.dest;
}

ExecResult native_call(ILFunction const &il, pFrame const &frame, CallDef const &instruction)
{
    intptr_t            depth { 0 };
    std::vector<ILType> types;
    for (auto const &arg : instruction.args) {
        auto type { arg.type };
        depth += alignat(size_of(type), 8);
        types.push_back(type);
    }
    auto return_type { instruction.target.type };
    auto args = frame->allocate(depth, 8);
    for (auto ptr = args; auto const &arg : instruction.args) {
        store(frame, ptr, arg);
        ptr = { static_cast<uint8_t *>(ptr) + alignat(size_of(arg.type), 8) };
    }
    auto     ret = frame->allocate(return_type);
    uint8_t *ret_ptr = static_cast<uint8_t *>(ret);
    auto    *args_ptr = static_cast<uint8_t *>(args);
    trace(L"Native call: {}(0x{:016x}) -> {}", instruction.name, reinterpret_cast<intptr_t>(args_ptr), return_type);
    frame->vm.dump_stack();
    if (native_call(as_utf8(instruction.name), args_ptr, types, ret_ptr, return_type)) {
        if (size_of(instruction.target.type) > 0) {
            assign(frame, instruction.target, frame->make_from_buffer(instruction.target, ret));
        }
        frame->release(args);
        ++frame->ip;
        return instruction.target;
    }
    return std::unexpected(ExecError {});
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, CallDef const &instruction)
{
    if (auto colon = instruction.name.rfind(L':'); colon != std::wstring_view::npos) {
        return native_call(il, frame, instruction);
    }

    std::vector<QBEValue> args;
    for (auto const &arg : instruction.args) {
        args.push_back(get(frame, arg));
    }

    auto find_and_execute = [&instruction, &frame, &args](auto const &file) -> ExecResult {
        for (auto const &fnc : file.functions) {
            if (fnc.name == instruction.name) {
                if (auto res = execute_qbe(frame->vm, frame->file, fnc, args); !res.has_value()) {
                    return std::unexpected(ExecError { res.error() });
                } else {
                    assign(frame, instruction.target, res.value());
                }
                ++frame->ip;
                return instruction.target;
            }
        }
        return std::unexpected(ExecError {});
    };

    if (auto res = find_and_execute(frame->file); res) {
        return res.value();
    } else if (!std::holds_alternative<ExecError>(res.error())
        || !std::get<ExecError>(res.error()).message.empty()) {
        return std::unexpected(res.error());
    }
    for (auto const &file : frame->vm.program.files) {
        if (auto res = find_and_execute(file); res) {
            return res.value();
        } else if (!std::holds_alternative<ExecError>(res.error())
            || !std::get<ExecError>(res.error()).message.empty()) {
            return std::unexpected(res.error());
        }
    }
    fatal(L"Function `{}` not found", instruction.name);
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, CopyDef const &instruction)
{
    assign(frame, instruction.target, get(frame, instruction.expr));
    ++frame->ip;
    return instruction.target;
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, ExprDef const &instruction)
{
    auto lhs = get(frame, instruction.lhs);
    auto rhs = get(frame, instruction.rhs);

    Operator lia_op;
    switch (instruction.op) {
#undef S
#define S(Op, Str, LiaOp) \
    case ILOperation::Op: \
        lia_op = LiaOp;   \
        break;
        ILOPERATIONS(S)
#undef S
    default:
        UNREACHABLE();
    }
    auto v = evaluate(lhs, lia_op, rhs);
    assign(frame, instruction.target, v);
    ++frame->ip;
    return instruction.target;
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, ExtDef const &instruction)
{
    auto src = get(frame, instruction.source);
    assert(std::holds_alternative<ILBaseType>(instruction.target.type.inner));
    auto target_type = std::get<ILBaseType>(instruction.target.type.inner);

    QBEValue target;
    if (is_integer(src.type) && is_integer(target_type)) {
        assert(size_of(target_type) > size_of(src.type));
        switch (src.type) {
        case ILBaseType::B:
        case ILBaseType::SB:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<int8_t>(src)) };
            break;
        case ILBaseType::UB:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<uint8_t>(src)) };
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<int16_t>(src)) };
            break;
        case ILBaseType::UH:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<uint16_t>(src)) };
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<int32_t>(src)) };
            break;
        case ILBaseType::UW:
            target = QBEValue { target_type, static_cast<intptr_t>(static_cast<uint32_t>(src)) };
            break;
        default:
            UNREACHABLE();
        }
    }
    assign(frame, instruction.target, target);
    ++frame->ip;
    return instruction.target;
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, JmpDef const &instruction)
{
    frame->ip = il.labels[instruction.label];
    return {};
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, JnzDef const &instruction)
{
    auto expr = get(frame, instruction.expr);
    frame->ip = (static_cast<bool>(expr))
        ? il.labels[instruction.on_true]
        : il.labels[instruction.on_false];
    return {};
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, LabelDef const &instruction)
{
    ++frame->ip;
    return {};
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, LoadDef const &instruction)
{
    auto src = get(frame, instruction.pointer);
    trace(L"Load value {:t} into {:t}", src, instruction.target);
    assign(frame, instruction.target, frame->make_from_buffer(instruction.target, src));
    ++frame->ip;
    return instruction.target;
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, RetDef const &instruction)
{
    if (instruction.expr) {
        auto retval { instruction.expr.value() };
        return std::unexpected(get(frame, retval));
    }
    return std::unexpected(QBEValue {});
}

template<>
ExecResult execute(ILFunction const &il, pFrame const &frame, StoreDef const &instruction)
{
    auto target = get(frame, instruction.target);
    store(frame, target, instruction.expr);
    ++frame->ip;
    return instruction.target;
}

ExecResult execute(ILFunction const &il, pFrame const &frame, ILInstruction const &instruction)
{
    trace(L"Executing {}", instruction);
    auto ret = std::visit(
        [&il, &frame](auto const &impl) -> ExecResult {
            return execute(il, frame, impl);
        },
        instruction.impl);
    frame->dump_frame();
    if (ret) {
        return ret.value();
    }
    return std::unexpected(ret.error());
}

ExecutionResult execute_qbe(VM &vm, ILFile const &file, ILFunction const &function, std::vector<QBEValue> const &args)
{
    auto frame { vm.new_frame(file, function) };
    for (auto const &[ix, arg] : std::ranges::views::enumerate(args)) {
        std::wstring name;
        name = function.parameters[ix].name;
        frame->arguments[std::format(L"param_{}", ix)] = arg;
    }
    trace("  Global Base  = 0x{:016x}", reinterpret_cast<intptr_t>(vm.data.data()));
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.strings)) {
        auto n = std::format(L"str_{}", ix + 1);
        if (!vm.globals[file.id].contains(n)) {
            assert((vm.data_pointer + (s.length() + 1) * sizeof(wchar_t)) <= VM::STACK_SIZE);
            QBEValue val { vm.data.data() + vm.data_pointer };
            trace(L"Set global {}.{} = offset 0x{:04x} ptr 0x{:016x}", file.id, n, vm.data_pointer, reinterpret_cast<intptr_t>(vm.data.data() + vm.data_pointer));
            for (auto ch : s) {
                *((wchar_t *) (vm.data.data() + vm.data_pointer)) = ch;
                vm.data_pointer += sizeof(wchar_t);
            }
            *((wchar_t *) (vm.data.data() + vm.data_pointer)) = 0;
            vm.data_pointer += sizeof(wchar_t);
            vm.globals[file.id][n] = val;
        }
    }
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.cstrings)) {
        auto n = std::format(L"cstr_{}", ix + 1);
        if (!vm.globals[file.id].contains(n)) {
            assert((vm.data_pointer + s.length() + 1) <= VM::STACK_SIZE);
            QBEValue val { vm.data.data() + vm.data_pointer };
            for (auto ch : s) {
                *((char *) (vm.data.data() + vm.data_pointer)) = ch;
                ++vm.data_pointer;
            }
            *((wchar_t *) (vm.data.data() + vm.data_pointer)) = 0;
            vm.data_pointer += sizeof(wchar_t);
            vm.globals[file.id][n] = val;
            trace(L"Set global {}.{} = {}", file.id, n, val);
        }
    }
    std::ranges::for_each(
        file.enumerations | std::views::enumerate,
        [&vm, &file](auto const &tuple) {
            auto const &[ix, enum_type] = tuple;
            auto            n = std::format(L"enum$_{}", ix + 1);
            QBEValue        val { vm.data.data() + vm.data_pointer };
            EnumType const &e = get<EnumType>(enum_type);
            *((uint64_t *) (vm.data.data() + vm.data_pointer)) = e.values.size();
            vm.data_pointer += sizeof(uint64_t);
            std::ranges::for_each(
                e.values,
                [&vm](EnumType::Value const &v) {
                    *((uint64_t *) (vm.data.data() + vm.data_pointer)) = v.value;
                    vm.data_pointer += sizeof(uint64_t);
                    *((uint64_t *) (vm.data.data() + vm.data_pointer)) = v.label.length();
                    vm.data_pointer += sizeof(uint64_t);
                });
            std::ranges::for_each(
                e.values,
                [&vm](EnumType::Value const &v) {
                    for (auto ch : v.label) {
                        *((wchar_t *) (vm.data.data() + vm.data_pointer)) = ch;
                        vm.data_pointer += sizeof(wchar_t);
                    }
                    *((wchar_t *) (vm.data.data() + vm.data_pointer)) = 0;
                    vm.data_pointer += sizeof(wchar_t);
                });
            vm.globals[file.id][n] = val;
            trace(L"Set global {}.{} = {}", file.id, n, val);
        });
    QBEValue base_pointer { vm.stack.data() + vm.stack_pointer };
    while (true) {
        trace(L"function `{}`[{}] ip {}", function.name, function.instructions.size(), frame->ip);
        if (auto res = execute(function, frame, function.instructions[frame->ip]); !res.has_value()) {
            return std::visit(
                overloads {
                    [&vm, &base_pointer, &function](QBEValue const &ret_value) -> ExecutionResult {
                        trace(L"function `{}` returns `{:tx}`", function.name, ret_value);
                        vm.release(base_pointer);
                        vm.dump_stack();
                        return ret_value;
                    },
                    [&vm, &base_pointer](ExecError const &error) -> ExecutionResult {
                        vm.release(base_pointer);
                        return std::unexpected(error.message);
                    } },
                res.error());
        } else {
            if (frame->ip >= function.instructions.size()) {
                vm.release(base_pointer);
                trace(L"function `{}` returns `{:tx}`", function.name, res.value());
                return get(frame, res.value());
            }
        }
    }
}

ExecutionResult execute_qbe(VM &vm)
{
    for (auto const &[file_ix, file] : std::ranges::views::enumerate(vm.program.files)) {
        if (file.has_main) {
            for (auto const &function : file.functions) {
                if (function.name == L"main") {
                    return execute_qbe(vm, file, function, {});
                }
            }
        }
    }
    return std::unexpected(L"No main function");
}
}
