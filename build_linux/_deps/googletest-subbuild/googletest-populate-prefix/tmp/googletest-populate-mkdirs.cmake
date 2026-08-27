# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-src"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-build"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/tmp"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/src"
  "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/c/Projects/Joker_protocol/build_linux/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
