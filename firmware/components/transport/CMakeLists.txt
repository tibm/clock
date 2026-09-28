# Wire formats for the app link: the command framing and the status snapshot.  Pure bytes
# in, bytes out -- no IDF, no services, no radio -- so every byte of the protocol is tested
# on the host.  The radio is hal::ble; the policy is the `net` AO.  [FIRMWARE.md §8]
set(CLK_TRANSPORT_SRCS src/frame.cpp src/snapshot.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_TRANSPORT_SRCS}
                           INCLUDE_DIRS include
                           REQUIRES core)
else()
    add_library(clk_transport STATIC ${CLK_TRANSPORT_SRCS})
    target_include_directories(clk_transport PUBLIC include)
    target_link_libraries(clk_transport PUBLIC clk_core)
endif()
