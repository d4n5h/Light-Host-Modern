include(FetchContent)
FetchContent_Declare(lame_src
    URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e)
FetchContent_GetProperties(lame_src)
if(NOT lame_src_POPULATED)
    FetchContent_Populate(lame_src)
endif()

set(LAME_SOURCES
    bitstream.c encoder.c fft.c gain_analysis.c id3tag.c lame.c newmdct.c presets.c
    psymodel.c quantize.c quantize_pvt.c reservoir.c set_get.c tables.c takehiro.c
    util.c vbrquantize.c VbrTag.c version.c)
list(TRANSFORM LAME_SOURCES PREPEND "${lame_src_SOURCE_DIR}/libmp3lame/")

add_library(mp3lame STATIC ${LAME_SOURCES})
target_include_directories(mp3lame PUBLIC
    "${lame_src_SOURCE_DIR}/include"
    "${lame_src_SOURCE_DIR}/libmp3lame"
    "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
if(MSVC)
    target_compile_options(mp3lame PRIVATE /w)
else()
    target_compile_options(mp3lame PRIVATE -w)
endif()
