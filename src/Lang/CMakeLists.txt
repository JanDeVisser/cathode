add_library(
        CathodeLang
        STATIC
        Coerce.cpp
        Config.cpp
        Dump.cpp
        Fold.cpp
        Keyword.cpp
        Namespace.cpp
        Operator.cpp
        Resolve.cpp
        Parser.cpp
        Stamp.cpp
        SyntaxNode.cpp
        Type.cpp
        AST/Call.cpp
        AST/Constant.cpp
        AST/ControlFlow.cpp
        AST/Defer.cpp
        AST/Expression.cpp
        AST/Function.cpp
        AST/Identifier.cpp
        AST/Module.cpp
        AST/PreProcess.cpp
        AST/Switch.cpp
        AST/Type.cpp
)

target_link_libraries(
        CathodeLang
        PRIVATE
        cathodert
        Util
        trampoline
        m
)

add_subdirectory(QBE)

include_directories(..)

install(TARGETS CathodeLang
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
