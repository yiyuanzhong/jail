# Copyright 2014 yiyuanzhong@gmail.com (Yiyuan Zhong)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


.PHONY: all clean

CPPFLAGS += -I../common

OBJECTS = child.o jaild.o ../common/libcommon.a

all: ../bin/jaild

clean:
	@rm -f ../bin/jaild
	@rm -f *.o

jaild.o: \
    ../common/array.h \
    ../common/cmdline.h \
    ../common/config.h \
    ../common/utility.h \
    jaild.h

child.o: \
    ../common/array.h \
    ../common/buffer.h \
    ../common/cmdline.h \
    ../common/config.h \
    ../common/environ.h \
    ../common/message.h \
    ../common/utility.h \
    jaild.h

../bin/jaild: $(OBJECTS)
	@echo LD $@
	@$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBRARIES)

%.o: %.c
	@echo CC $<
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
