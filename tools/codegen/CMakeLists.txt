set(LLVM_LINK_COMPONENTS support)

find_package(protobuf CONFIG REQUIRED)

add_executable(cdt-codegen cdt-codegen.cpp)
set_property(TARGET cdt-codegen PROPERTY CXX_STANDARD 23)
target_compile_options(cdt-codegen PRIVATE -fexceptions -fno-rtti)
target_compile_definitions(cdt-codegen PRIVATE
   CDT_VERSION="${PROJECT_VERSION}"
   SHARED_LIB_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
target_include_directories(cdt-codegen PRIVATE
   ${CMAKE_CURRENT_SOURCE_DIR}/../include
   ${LLVM_INCLUDE_DIRS}
)
target_include_directories(cdt-codegen SYSTEM PRIVATE
   ${CMAKE_CURRENT_SOURCE_DIR}/../jsoncons/include
)
target_link_libraries(cdt-codegen PRIVATE LLVMSupport protobuf::libprotobuf)

add_custom_command(TARGET cdt-codegen POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cdt-codegen> ${CMAKE_BINARY_DIR}/bin/
)
