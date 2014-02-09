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

OBJECTS = jail.o ../common/libcommon.a

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

../bin/jail: $(OBJECTS)
	@echo LD $@
	@$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBRARIES)

%.o: %.c
	@echo CC $<
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
