FLAGS:=-g -fsanitize=address,undefined,fuzzer -fno-sanitize-recover=all -I.. -std=c++20
CXX:=clang++-14

all: fuzzer_decompress0 fuzzer_compress0 fuzzer_roundtrip0

fuzzer_decompress0: fuzzer_decompress.cpp ../lzav.h
	$(CXX) $(FLAGS) -O0 $< -o $@
fuzzer_compress0: fuzzer_compress.cpp ../lzav.h
	$(CXX) $(FLAGS) -O0 $< -o $@
fuzzer_roundtrip0: fuzzer_roundtrip.cpp ../lzav.h
	$(CXX) $(FLAGS) -O0 $< -o $@

