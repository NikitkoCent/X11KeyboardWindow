#   Copyright 2022 Nikita Provotorov
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.


cmake_minimum_required(VERSION 3.14)

project(X11KeyboardWindow CXX)

find_package(X11 REQUIRED)

add_executable(X11KeyboardWindow main.cpp)

set_target_properties(X11KeyboardWindow PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if ((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_compile_options(X11KeyboardWindow
        PRIVATE -Wall        # basic set of warnings
        PRIVATE -Wextra      # additional warnings
        PRIVATE -pedantic    # modern C++ inspections
        PRIVATE -Werror      # treat all warnings as errors
    )
endif()

target_link_libraries(X11KeyboardWindow
    PRIVATE X11::X11
)
