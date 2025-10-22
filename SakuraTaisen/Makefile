PROJECT := SakuraTaisen

# Sources
SRCS := SakuraTaisen.cpp \
        ../Utils/Utils.cpp \
        ../Utils/prs-decomp.cpp \
        ../ExternalTools/lodepng.cpp

# Common flags
CXXFLAGS_COMMON := -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -fdiagnostics-color=always \
                   -I. -I.. -I../Utils -I../ExternalTools
LDFLAGS_COMMON  :=

SRC_REL := $(patsubst ../%,%,$(SRCS))

# ---- MacOS ----
MAC_CXX      := /usr/bin/clang++
MAC_CXXFLAGS := $(CXXFLAGS_COMMON) -stdlib=libc++
MAC_LDFLAGS  := $(LDFLAGS_COMMON)  -stdlib=libc++

MAC_OBJDIR := build/mac
MAC_OBJS   := $(addprefix $(MAC_OBJDIR)/,$(SRC_REL:.cpp=.o))
MAC_BIN    := $(MAC_OBJDIR)/$(PROJECT)

.PHONY: mac
mac: $(MAC_BIN)

$(MAC_BIN): $(MAC_OBJS)
	@mkdir -p $(dir $@)
	$(MAC_CXX) $(MAC_LDFLAGS) $^ -o $@

$(MAC_OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(MAC_CXX) $(MAC_CXXFLAGS) -c $< -o $@

$(MAC_OBJDIR)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(MAC_CXX) $(MAC_CXXFLAGS) -c $< -o $@

# ====== Linux build ======
LINUX_CXX      := g++
LINUX_CXXFLAGS := $(CXXFLAGS_COMMON) -stdlib=libc++
LINUX_LDFLAGS  := $(LDFLAGS_COMMON)  -stdlib=libc++

LINUX_OBJDIR   := build/linux
LINUX_OBJS     := $(addprefix $(LINUX_OBJDIR)/,$(SRC_REL:.cpp=.o))
LINUX_TARGET   := $(LINUX_OBJDIR)/$(PROJECT)

.PHONY: linux
linux: $(LINUX_TARGET)

$(LINUX_TARGET): $(LINUX_OBJS)
	@mkdir -p $(dir $@)
	@$(LINUX_CXX) $(LINUX_CXXFLAGS) $(LINUX_OBJS) -o $@

$(LINUX_OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(LINUX_CXX) $(LINUX_CXXFLAGS) $(LINUX_LDFLAGS) -c $< -o $@

$(LINUX_OBJDIR)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	@$(LINUX_CXX) $(LINUX_CXXFLAGS) $(LINUX_LDFLAGS) -c $< -o $@


# ---- Win (mingw-w64) ----
WIN_CXX     := x86_64-w64-mingw32-g++
WIN_LDFLAGS := -static -static-libstdc++ -static-libgcc -lwinpthread
WIN_OBJDIR  := build/win
WIN_OBJS    := $(addprefix $(WIN_OBJDIR)/,$(SRC_REL:.cpp=.o))
WIN_BIN     := $(WIN_OBJDIR)/$(PROJECT).exe

.PHONY: win
win: $(WIN_BIN)

$(WIN_BIN): $(WIN_OBJS)
	@mkdir -p $(dir $@)
	$(WIN_CXX) $(CXXFLAGS_COMMON) $^ -o $@ $(LDFLAGS_COMMON)

$(WIN_OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(WIN_CXX) $(CXXFLAGS_COMMON) -c $< -o $@

$(WIN_OBJDIR)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(WIN_CXX) $(CXXFLAGS_COMMON) -c $< -o $@

# ---- convenience ----
.PHONY: all clean run-wine
all: mac
run-wine: win
	wine64 $(WIN_BIN)
clean:
	rm -rf build