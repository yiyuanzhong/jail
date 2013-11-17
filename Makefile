.PHONY: common server client all clean

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
