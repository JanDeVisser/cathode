add_library(
        Util
        STATIC
        Align.h
        Arena.cpp
        Checked.h
        Defer.h
        Enumerate.h
        Error.cpp
        #       Integer.h
        IO.cpp
        JSON.cpp
        Lexer.h
        Logging.cpp
        Options.cpp
        Pipe.cpp
        Process.cpp
        Resolve.cpp
        StringScanner.h
        StringUtil.cpp
        Token.cpp
        TokenLocation.h
        Utf8.cpp
)

target_link_libraries(
        Util
        ${ICONV}
        dl
)

include_directories(..)

install(TARGETS Util
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
