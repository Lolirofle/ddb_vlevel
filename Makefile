OUT?=ddb_vlevel.so

CC?=g++
CFLAGS+=-Wall -g -fPIC -std=c++11 -D_GNU_SOURCE
LDFLAGS+=-shared

OUT_DIR?=out

SOURCES?=$(wildcard *.cpp)
OBJS?=$(patsubst %.cpp, $(OUT_DIR)/%.o, $(SOURCES))

define compile
	$(CC) $(CFLAGS) $1 $2 $< -c -o $@
endef

define link
	$(CC) $(LDFLAGS) $1 $2 $3 -o $@
endef

all: out

out: mkdir_out $(SOURCES) $(OUT_DIR)/$(OUT)

mkdir_out:
	@echo "Creating build directory"
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/$(OUT): $(OBJS)
	@echo "Linking"
	@$(call link, $(OBJS))
	@echo "Done!"

$(OUT_DIR)/%.o: %.cpp
	@echo "Compiling $(subst $(OUT_DIR)/,,$@)"
	@$(call compile)

clean:
	@echo "Cleaning files from previous build..."
	@rm -r -f $(OUT_DIR)
