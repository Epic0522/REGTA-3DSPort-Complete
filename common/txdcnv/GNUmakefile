default: txdcnv


SOURCES		:=	txdcnv.cpp                             \
			Directory.cpp                          \
			$(wildcard ../librw/*.cpp)             \
			$(wildcard ../librw/src/*.cpp)         \
			$(wildcard ../librw/src/d3d/*.cpp)     \
			$(wildcard ../librw/src/gl/*.cpp)      \
			$(wildcard ../librw/src/ps2/*.cpp)     \
			$(wildcard ../librw/src/3ds/*.cpp)     \
			$(wildcard ../librw/src/3ds/tex/*.cpp) \
			$(wildcard ../librw/src/lodepng/*.cpp)

OBJECTS		:=	$(foreach file,$(SOURCES),$(basename $(file)).o)
OBJECTS		:=	$(foreach file,$(OBJECTS),obj/$(patsubst ../%,%,$(file)))
CXXFLAGS	:=	-O4 -flto -DRW_NULL -I../librw -fpermissive
LINK_OPT	:=

obj/%.o: ../%.cpp
	@mkdir -p $(dir $@) || true
	g++ -c $< -o $@ $(CXXFLAGS)

obj/%.o: ./%.cpp
	@mkdir -p $(basename $@) || true
	g++ -c $< -o $@ $(CXXFLAGS)

txdcnv: $(OBJECTS)
	@g++ -o txdcnv $(OBJECTS)

.PHONY: info
info:
	@echo $(OBJECTS)
