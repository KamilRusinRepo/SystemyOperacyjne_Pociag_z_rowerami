all: kierownik pasazerowie zawiadowca

zawiadowca: zawiadowca.c
	gcc zawiadowca.c -o zawiadowca
kierownik: kierownik.c
	gcc kierownik.c -o kierownik

pasazerowie: pasazerowie.c
	gcc pasazerowie.c -o pasazerowie

clean:
	rm -f kierownik pasazerowie
