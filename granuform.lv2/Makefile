CC ?= gcc
CFLAGS ?= -Wall -O3 -fPIC `pkg-config --cflags lv2`
LDFLAGS += -shared

all: granuform.so

granuform.so: granuform.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm

install: all
	mkdir -p ~/.lv2/granuform.lv2
	cp granuform.so granuform.ttl manifest.ttl ~/.lv2/granuform.lv2/

clean:
	rm -f granuform.so
