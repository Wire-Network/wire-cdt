find_package(protobuf CONFIG REQUIRED)

# Generate zpp_options.pb.h/cc from zpp_options.proto
set(ZPP_OPTIONS_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/zpp_options.proto)
set(ZPP_OPTIONS_PB_H ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.h)
set(ZPP_OPTIONS_PB_CC ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.cc)

find_program(PROTOC_PROGRAM protoc REQUIRED)

add_custom_command(
   OUTPUT ${ZPP_OPTIONS_PB_H} ${ZPP_OPTIONS_PB_CC}
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/zpp
   COMMAND ${PROTOC_PROGRAM}
      --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
      --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/zpp
      ${ZPP_OPTIONS_PROTO}
   # protoc generates files without the zpp/ prefix in names, move them
   COMMAND ${CMAKE_COMMAND} -E rename
      ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.h
      ${ZPP_OPTIONS_PB_H}
   COMMAND ${CMAKE_COMMAND} -E rename
      ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.cc
      ${ZPP_OPTIONS_PB_CC}
   DEPENDS ${ZPP_OPTIONS_PROTO}
   COMMENT "Generating zpp_options protobuf sources"
)

add_executable(protoc-gen-zpp protoc-gen-zpp.cpp ${ZPP_OPTIONS_PB_CC})
target_include_directories(protoc-gen-zpp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(protoc-gen-zpp PRIVATE protobuf::libprotoc protobuf::libprotobuf)

add_custom_command(TARGET protoc-gen-zpp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:protoc-gen-zpp> ${CMAKE_BINARY_DIR}/bin/
)

# Copy zpp_options.proto to include dir so contracts can import it
add_custom_command(TARGET protoc-gen-zpp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include/zpp
   COMMAND ${CMAKE_COMMAND} -E copy ${ZPP_OPTIONS_PROTO} ${CMAKE_BINARY_DIR}/include/zpp/
)
