CC=gcc
CFLAGS=-Wall -Wextra

all: kierownik pasazerowie zawiadowca

zawiadowca: zawiadowca.c
	$(CC) $(CFLAGS) zawiadowca.c -o zawiadowca
kierownik: kierownik.c
	$(CC) $(CFLAGS) kierownik.c -o kierownik

pasazerowie: pasazerowie.c
	$(CC) $(CFLAGS) pasazerowie.c -o pasazerowie

clean:
	rm -f kierownik pasazerowie
