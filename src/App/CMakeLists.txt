add_executable(
        cathode
        Main.cpp
)

target_link_libraries(
        cathode
        PRIVATE
        CathodeQBE
)

include_directories(..)

#add_compile_options("-fno-inline-functions")

install(TARGETS cathode
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

