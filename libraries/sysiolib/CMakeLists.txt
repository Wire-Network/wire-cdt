file(GLOB HEADERS "*.hpp"
                  "*.h")

add_library(sysio
            sysiolib.cpp
            crypto.cpp
            ${HEADERS})

add_library(sysio_malloc
            malloc.cpp
            ${HEADERS})

add_library(sysio_dsm
            simple_malloc.cpp
            ${HEADERS})

add_library(sysio_cmem
            memory.cpp
            ${HEADERS})


set_target_properties(sysio_malloc PROPERTIES LINKER_LANGUAGE C)

target_include_directories(sysio PUBLIC
                                 ${CMAKE_SOURCE_DIR}/libc/cdt-musl/include
                                 ${CMAKE_SOURCE_DIR}/libc/cdt-musl/src/internal
                                 ${CMAKE_SOURCE_DIR}/libc/cdt-musl/src/crypt
                                 ${CMAKE_SOURCE_DIR}/libc/cdt-musl/arch/eos
                                 ${CMAKE_SOURCE_DIR}/libc++/cdt-libcxx/include
                                 ${CMAKE_SOURCE_DIR}
                                 ${CMAKE_SOURCE_DIR}/boost/include)

target_link_libraries( sysio c c++ )
add_custom_command( TARGET sysio POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sysio> ${BASE_BINARY_DIR}/lib )
add_custom_command( TARGET sysio_malloc POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sysio_malloc> ${BASE_BINARY_DIR}/lib )
add_custom_command( TARGET sysio_dsm POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sysio_dsm> ${BASE_BINARY_DIR}/lib )
add_custom_command( TARGET sysio_cmem POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sysio_cmem> ${BASE_BINARY_DIR}/lib )

if (ENABLE_NATIVE_COMPILER)
   add_native_library(native_sysio
                      sysiolib.cpp
                      crypto.cpp
                      malloc.cpp
                      ${HEADERS})

   add_dependencies( native_sysio sysio )
   add_custom_command( TARGET native_sysio POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_sysio> ${BASE_BINARY_DIR}/lib )
endif()

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../sysiolib DESTINATION ${BASE_BINARY_DIR}/include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
