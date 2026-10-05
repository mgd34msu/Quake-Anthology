file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
file(WRITE "${OUTPUT}" "#include \"qa/common.h\"\nconst uint8_t ${SYMBOL}_data[] = {${bytes}};\n")
