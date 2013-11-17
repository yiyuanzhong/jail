.PHONY: all clean

CFLAGS += -g -Wall
CFLAGS += -I../common
LDFLAGS += -L../common
LIBRARIES += -lcommon

OBJECTS = jail.o

all: ../bin/jail

clean:
	@rm -f ../bin/jail
	@rm -f *.o

jail.o: \
    ../common/array.h \
    ../common/buffer.h \
    ../common/config.h \
    ../common/environ.h \
    ../common/message.h

../bin/jail: $(OBJECTS) ../common/libcommon.a
	@echo LD $@
	@$(CC) $(LDFLAGS) -Wl,--version-script=../hideall.version -o $@ $(OBJECTS) $(LIBRARIES)

%.o: %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<
