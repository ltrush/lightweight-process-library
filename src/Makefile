CFLAGS = -Wall -Wextra -Werror -g -fPIC

all: liblwp.so

liblwp.a: lwp.o rr.o magic64.o
	ar rcs liblwp.a lwp.o rr.o magic64.o

liblwp.so: lwp.o rr.o magic64.o
	gcc -shared -o liblwp.so lwp.o rr.o magic64.o

lwp.o: lwp.c lwp.h
	gcc $(CFLAGS) -c lwp.c -o lwp.o

rr.o: rr.c rr.h lwp.h
	gcc $(CFLAGS) -c rr.c -o rr.o

magic64.o: magic64.S
	gcc -c magic64.S

clean:
	rm -f *.o *.a *.so core.* DETAILS* test .nfs*
