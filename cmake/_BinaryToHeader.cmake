# cmake/_BinaryToHeader.cmake
# Usage: cmake -DINPUT=path/to.blob -DOUTPUT=path/to/header.h -P this_script

file(READ "${INPUT}" _bytes HEX)
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," _csv "${_bytes}")
get_filename_component(_name "${OUTPUT}" NAME_WE)
file(WRITE "${OUTPUT}"
"// generated from ${INPUT}\n#include <cstdint>\nstatic const uint8_t ${_name}[] = { ${_csv} };\n")
