set(sqlite-wrapper_src
    internal/implementation.cpp
    Database.cpp
    PreparedStatement.cpp
)
add_library(sqlite-wrapper-cpp STATIC ${sqlite-wrapper_src})
set_target_properties(sqlite-wrapper-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(sqlite-wrapper-cpp ${SQLite3_LIBRARY})
