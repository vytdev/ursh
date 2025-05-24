# ursh's Makefile.
#

CC=       gcc
RM=       rm -rf
STD=      gnu99

CFLAGS=   -Wall -Wpedantic

CFLAGS+=  $(MYCFLAGS) -std=$(STD)
LDFLAGS+= $(MYLDFLAGS)

SRV-SRC= urshd.c common.c
SRV-OBJ= $(SRV-SRC:.c=.o)
SRV-TRG= urshd

CLN-SRC= ursh.c common.c
CLN-OBJ= $(CLN-SRC:.c=.o)
CLN-TRG= ursh

default: release
build: $(SRV-TRG) $(CLN-TRG)

release:  CFLAGS+=  -O2
release:  build
debug:    CFLAGS+=  -g -DDEBUG_
debug:    LDFLAGS+= -g
debug:    build

$(SRV-TRG): $(SRV-OBJ)
$(CLN-TRG): $(CLN-OBJ)
$(SRV-TRG) $(CLN-TRG):
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(SRV-TRG) $(CLN-TRG) $(SRV-OBJ) $(CLN-OBJ)

.PHONY: default build release debug clean
