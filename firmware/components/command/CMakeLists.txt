if(ESP_PLATFORM)
    idf_component_register(SRCS src/command.cpp
                           INCLUDE_DIRS include
                           REQUIRES core)
else()
    add_library(clk_command STATIC src/command.cpp)
    target_include_directories(clk_command PUBLIC include)
    target_link_libraries(clk_command PUBLIC clk_core)
endif()
