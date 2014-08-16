CXX=g++   # substitute clang++ if you prefer

gctests: gctests.cpp gc.cpp
	$(CXX) -std=c++11 -O3 -o $@ gctests.cpp
