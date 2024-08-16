#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

add_library(kleeFuzzerIntrinsics SHARED
  fuzzer_intrinsics.c
  # Mocks:
  ${CMAKE_SOURCE_DIR}/runtime/Mocks/models.c
  # HACK:
  ${CMAKE_SOURCE_DIR}/lib/Basic/KTest.cpp
)
# Increment version appropriately if ABI/API changes, more details:
# http://tldp.org/HOWTO/Program-Library-HOWTO/shared-libraries.html#AEN135
set(KLEE_FUZZER_INTRINSICS_VERSION 1.0)
set_target_properties(kleeFuzzerIntrinsics
  PROPERTIES
    VERSION ${KLEE_FUZZER_INTRINSICS_VERSION}
    SOVERSION ${KLEE_FUZZER_INTRINSICS_VERSION}
)
target_include_directories(kleeFuzzerIntrinsics PRIVATE ${KLEE_INCLUDE_DIRS})

install(TARGETS kleeFuzzerIntrinsics DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
