set(srcs "tusb_msc_main.cpp")
set(requires fatfs console M5GFX M5Unified)

if(CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH)
    list(APPEND requires wear_levelling)
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS .
    REQUIRES "${requires}"
)
