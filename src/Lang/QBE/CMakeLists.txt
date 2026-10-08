add_library(
        CathodeQBE
        STATIC
        Context.cpp
        Execute.cpp
        Expr.cpp
        File.cpp
        Function.cpp
        Instruction.cpp
        Native.cpp
        Operator.cpp
        QBE.cpp
        Type.cpp
        Value.cpp
        Generate/ArgumentList.cpp
        Generate/BinaryExpression.cpp
        Generate/Block.cpp
        Generate/Break.cpp
        Generate/Call.cpp
        Generate/CapturedPayload.cpp
        Generate/Comptime.cpp
        Generate/Constant.cpp
        Generate/DeferStatement.cpp
        Generate/ExpressionList.cpp
        Generate/ForStatement.cpp
        Generate/Function.cpp
        Generate/Generate.cpp
        Generate/Identifier.cpp
        Generate/IfStatement.cpp
        Generate/Module.cpp
        Generate/LoopStatement.cpp
        Generate/Program.cpp
        Generate/Return.cpp
        Generate/SwitchStatement.cpp
        Generate/TagValue.cpp
        Generate/Type.cpp
        Generate/UnaryExpression.cpp
        Generate/VariableDeclaration.cpp
        Generate/Visibility.cpp
        Generate/WhileStatement.cpp
)

target_link_libraries(
        CathodeQBE
        PRIVATE
        cathodert
        Util
        CathodeLang
        trampoline
        m
)

include_directories(../..)

install(TARGETS CathodeQBE
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
