HCMPATH=$(shell pwd)/../

CXXFLAGS=-Wall -pedantic -ggdb -O0 -fPIC -I$(HCMPATH)/include  -I$(HCMPATH)/flattener
CFLAGS=  -Wall -pedantic -ggdb -O0 -fPIC -I$(HCMPATH)/include  -I$(HCMPATH)/flattener
CC=g++
LDFLAGS=-L$(HCMPATH)/src -lhcm -Wl,-rpath=$(HCMPATH)/src

all: gl_stat gl_rank

gl_stat: HW1ex1.o $(HCMPATH)/flattener/flat.o
	g++ -o $@ $^ $(LDFLAGS)

gl_rank: HW1ex2.o $(HCMPATH)/flattener/flat.o
	g++ -o $@ $^ $(LDFLAGS)
 
clean: 
	@ rm gl_stat gl_rank $(wildcard *.o) \
	$(wildcard *.so) $(wildcard *.d) $(wildcard *~) || true
