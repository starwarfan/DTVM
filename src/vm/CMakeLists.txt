# Copyright (C) 2025-2026 the DTVM authors. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set(VM_SRCS dt_vm.cpp)

add_library(dtvmapi SHARED ${VM_SRCS})

target_link_libraries(dtvmapi PRIVATE dtvmcore CLI11::CLI11)

set_target_properties(
  dtvmapi PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
