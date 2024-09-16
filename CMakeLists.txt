# This file sets up builds if libcanard is used
# as an ESP-IDF component.

idf_component_register(SRCS "libcanard/canard.c"
                       INCLUDE_DIRS "libcanard/")

# Apply the Kconfig options to the source file.

if(NOT CONFIG_CANARD_ASSERTIONS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "CANARD_ASSERT=(void)")
endif()

if(CONFIG_CANARD_CRC_TABLE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CANARD_CRC_TABLE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CANARD_CRC_TABLE=0)
endif()
