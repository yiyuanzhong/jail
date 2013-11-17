.PHONY: all clean

CFLAGS += -g -Wall
CFLAGS += -I../common
LDFLAGS += -L../common
LIBRARIES += -lcommon -lutil

OBJECTS = child.o jaild.o

all: ../bin/jaild

clean:
	@rm -f ../bin/jaild
	@rm -f *.o

jaild.o: \
    ../common/cmdline.h \
    ../common/config.h \
    ../common/logger.h \
    ../common/utility.h \
    jaild.h

child.o: \
    ../common/array.h \
    ../common/buffer.h \
    ../common/cmdline.h \
    ../common/config.h \
    ../common/environ.h \
    ../common/logger.h \
    ../common/message.h \
    ../common/utility.h \
    jaild.h

../bin/jaild: $(OBJECTS) ../common/libcommon.a
	@echo LD $@
	@$(CC) $(LDFLAGS) -Wl,--version-script=../hideall.version -o $@ $(OBJECTS) $(LIBRARIES)

%.o: %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<
