find_package(protobuf CONFIG REQUIRED)

# Generate zpp_options.pb.h/cc from zpp_options.proto
set(ZPP_OPTIONS_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/zpp_options.proto)
set(ZPP_OPTIONS_PB_H ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.h)
set(ZPP_OPTIONS_PB_CC ${CMAKE_CURRENT_BINARY_DIR}/zpp/zpp_options.pb.cc)

set(PROTOC_PROGRAM $<TARGET_FILE:protobuf::protoc>)

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

add_executable(cdt-protoc-gen-zpp protoc-gen-zpp.cpp ${ZPP_OPTIONS_PB_CC})
target_include_directories(cdt-protoc-gen-zpp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cdt-protoc-gen-zpp PRIVATE protobuf::libprotoc protobuf::libprotobuf)

add_custom_command(TARGET cdt-protoc-gen-zpp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cdt-protoc-gen-zpp> ${CMAKE_BINARY_DIR}/bin/
)

# Also install protoc from vcpkg into CDT bin/ as cdt-protoc
add_custom_command(TARGET cdt-protoc-gen-zpp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy ${PROTOC_PROGRAM} ${CMAKE_BINARY_DIR}/bin/cdt-protoc
)

# Copy zpp_options.proto to include dir so contracts can import it
add_custom_command(TARGET cdt-protoc-gen-zpp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include/zpp
   COMMAND ${CMAKE_COMMAND} -E copy ${ZPP_OPTIONS_PROTO} ${CMAKE_BINARY_DIR}/include/zpp/
)
