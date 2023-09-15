cmake_minimum_required (VERSION 3.5.1)

project (rtxfluid)
add_library(flip SHARED flip.c database.c)
add_executable(rtxfluid rtxfluid.c plugin.c database.c)
