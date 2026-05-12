#Build artifact type, possible values shared, static and exe
ARTIFACT_TYPE = exe

#Executables to build
EXECUTABLES = SW_WDG SW_Producer

#Build architecture/variant string, possible values: x86, armv7le, etc... aarch64le
PLATFORM ?= aarch64le

#Build profile, possible values: release, debug, profile, coverage
BUILD_PROFILE ?= debug

CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)
OUTPUT_DIR = build/$(CONFIG_NAME)

#Target executables
TARGETS = $(addprefix $(OUTPUT_DIR)/,$(EXECUTABLES))

#Source mapping - which sources go to which executable
SW_WDG_SRCS = SW_Watchdog.cpp
SW_Producer_SRCS = SW_Producer.cpp

#Object files for each executable
SW_WDG_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(basename $(SW_WDG_SRCS))))
SW_Producer_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(basename $(SW_Producer_SRCS))))

#Compiler definitions

CC = qcc -Vgcc_nto$(PLATFORM)
CXX = q++ -Vgcc_nto$(PLATFORM)_cxx

LD = $(CXX)

#User defined include/preprocessor flags and libraries

#INCLUDES += -I/path/to/my/lib/include
#INCLUDES += -I../mylib/public

#LIBS += -L/path/to/my/lib/$(PLATFORM)/usr/lib -lmylib
#LIBS += -L../mylib/$(OUTPUT_DIR) -lmylib

#Compiler flags for build profiles
CCFLAGS_release += -O2
CCFLAGS_debug += -g -O0 -fno-builtin
CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs
CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

#Generic compiler flags (which include build type flags)
CCFLAGS_all += -Wall -fmessage-length=0 -fPIC
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))

LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))
DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

#Compiling rule for c
$(OUTPUT_DIR)/%.o: %.c
	-@mkdir -p $(OUTPUT_DIR)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

#Compiling rule for c++
$(OUTPUT_DIR)/%.o: %.cpp
	-@mkdir -p $(OUTPUT_DIR)
	$(CXX) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

#Linking rule for SW_WDG
$(OUTPUT_DIR)/SW_WDG: $(SW_WDG_OBJS)
	$(LD) -o $@ $(LDFLAGS_all) $(LDFLAGS) $^ $(LIBS_all) $(LIBS)

#Linking rule for SW_Producer
$(OUTPUT_DIR)/SW_Producer: $(SW_Producer_OBJS)
	$(LD) -o $@ $(LDFLAGS_all) $(LDFLAGS) $^ $(LIBS_all) $(LIBS)

#Rules section for default compilation and linking
all: $(TARGETS)

CLEAN_DIRS := $(shell find build -type d)
CLEAN_PATTERNS := *.o *.d $(EXECUTABLES)
CLEAN_FILES := $(foreach DIR,$(CLEAN_DIRS),$(addprefix $(DIR)/,$(CLEAN_PATTERNS)))

clean:
	rm -f $(CLEAN_FILES)

rebuild: clean all

#Inclusion of dependencies (object files to source and includes)
-include $(SW_WDG_OBJS:%.o=%.d)
-include $(SW_Producer_OBJS:%.o=%.d)