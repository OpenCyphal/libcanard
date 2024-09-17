# This file is used when packaging libcanard
# for the ESP-IDF component registry:
# https://components.espressif.com/

idf_component_register(SRCS "canard.c"
                       INCLUDE_DIRS "include")

# Apply the Kconfig options to the source file.

if(NOT CONFIG_CANARD_ASSERTIONS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "CANARD_ASSERT=(void)")
endif()

if(CONFIG_CANARD_CRC_TABLE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CANARD_CRC_TABLE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CANARD_CRC_TABLE=0)
endif()
