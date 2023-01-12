cmake_minimum_required(VERSION 3.17.0)

project(Ruby LANGUAGES C ASM)
add_library(ruby-static STATIC
  ruby.c)
