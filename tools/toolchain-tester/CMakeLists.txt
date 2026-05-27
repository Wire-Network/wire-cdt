cmake_minimum_required(VERSION 3.15)
project(toolchain-tester)

configure_file(toolchain-tester.in toolchain-tester )
add_custom_target(toolchain-tester ALL)
