# Header-only for now: hand arithmetic is constexpr and has nothing to link.  It still gets
# a component so the §2 dependency rule is enforced by CMake rather than by good intentions --
# domain/ may see hal/api (for kUstepsPerRev) and core/, and nothing else.
if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include REQUIRES core clk_hal)
else()
    add_library(clk_domain INTERFACE)
    target_include_directories(clk_domain INTERFACE include)
    target_link_libraries(clk_domain INTERFACE clk_core clk_hal)
endif()
