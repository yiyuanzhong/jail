.PHONY: all clean

CFLAGS += -g -Wall
OBJECTS = array.o buffer.o cmdline.o config.o environ.o logger.o utility.o

all: libcommon.a
clean:
	@rm -f *.a
	@rm -f *.o

%.o: %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<

libcommon.a: $(OBJECTS)
	@echo AR $@
	@$(AR) rcs $@ $(OBJECTS)
