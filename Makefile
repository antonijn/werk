TARGET = werk

OBJECTS = src/main.o src/edit.o src/gap.o src/ncurses.o src/cfgprs.o \
          src/cfg.o src/sparsef.o src/mode.o
LIBS = ncurses
CFLAGS = -DHAS_NCURSES -Wreturn-type -Wunused-function

ifndef GTK
GTK=true
endif

ifeq ($(GTK), true)
OBJECTS += src/gtk.o
CFLAGS += -DHAS_GTK
LIBS += gtk+-3.0
endif

CC = gcc
PKG-CONFIG = pkg-config
CFLAGS += $(shell $(PKG-CONFIG) --cflags $(LIBS)) \
	-funsigned-char -std=c11 -Wno-pointer-sign -pedantic-errors
LD = $(CC)
LDFLAGS += $(shell $(PKG-CONFIG) --libs $(LIBS)) -lunistring

release: CFLAGS += -DNDEBUG -O2
debug: CFLAGS += -g
debug: LDFLAGS += -rdynamic

release debug: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -f $(TARGET) $(OBJECTS)
