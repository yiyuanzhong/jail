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


.PHONY: common server client all clean

export CFLAGS = -g -O2 -Wall -Wextra -fvisibility=hidden
export CPPFLAGS = -D_GNU_SOURCE -DNDEBUG

all: mrproper common server client

mrproper:
	@mkdir -p bin

clean:
	@$(MAKE) -C client clean
	@$(MAKE) -C server clean
	@$(MAKE) -C common clean
	@rm -rf bin

common:
	@$(MAKE) -C common

server:
	@$(MAKE) -C server

client:
	@$(MAKE) -C client
