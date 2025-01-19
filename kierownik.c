#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

//nazwy semaforow
#define PERON 0
#define BAGAZ 1
#define ROWER 2
#define POCIAG 3
#define SH 4

//zmienne globalne
key_t kluczM, kluczS;
int* sh;
int semID, shmID, wielkoscPamieci, pociagi, odjazd, przyjazd, pasazerowieWpociagu, roweryWpociagu, przerwanie;

//indeksy przechowywujace miejsce do zapisu paszerow
#define ZAPISB sh[wielkoscPamieci - 5]
#define ZAPISR sh[wielkoscPamieci - 4]
#define ZAPISP wielkoscPamieci - 3
#define BLOKADA wielkoscPamieci - 2

//funkcja obslugujaca sygnal wysylany z procesu ZAWIADOWCA
void obslugaSygnal1(int sig) {
	printf("Pociag obsluzyl sygnal\n");
	przerwanie = 1;
}

//funkcja obslugujaca operacje na sempaforach
void sem_op(int sem_id, int sem_num, int op) {
    struct sembuf sops = {sem_num, op, 0};
    int wynik = semop(sem_id, &sops, 1);
    if (wynik == -1) {
	if(errno == EINTR) {
		sem_op(sem_id, sem_num, op);
	}
	else {
        	perror("Błąd operacji na semaforze");
        	exit(1);
	}
    }

}

//obsluga procesu pociag
void pociag() {
    //obsluga sygnalu SIGUSR2 - oznaczajaca odjazd pociagu znajdujacego sie na peronie
    struct sigaction sigUsr2Handler;
    sigUsr2Handler.sa_handler = obslugaSygnal1;
    sigemptyset(&sigUsr2Handler.sa_mask);
    sigUsr2Handler.sa_flags = 0;
    sigaction(SIGUSR2, &sigUsr2Handler, NULL);

    //informacja - pociag o numerze PID zostal stworzony
    printf("Pociag zostal stworzony. NR %d\n", getpid());

    int i = 0;

    do {
	przerwanie = 0;
        //pociag wjezdza na peron
        sem_op(semID, POCIAG, -1);
        printf("Pociag %d dojechal na peron\n", getpid());


	//pociag wpisuje swoje ID do pamieci dzielonej by mozna bylo do niego wyslac sygnal SIGUSR2
	sem_op(semID, SH, -1);
        printf("Wpisywanie pociagu do pamieci\n");
	sh[wielkoscPamieci - 1] = getpid();
	sem_op(semID, SH, 1);

        //pociag daje mozliwosc wejscia pasazerom
        semctl(semID, BAGAZ, SETVAL, pasazerowieWpociagu);
        semctl(semID, ROWER, SETVAL, roweryWpociagu);
	printf("Pociag %d czeka na pasazerow\n", getpid());

        //start odliczania czasu postoju na peronie
        for(int i = 0; i < odjazd; i++) {
		sleep(1);
		if(przerwanie) {
			break;
		}
	}

	//zamkniecie kolejek - pasazer nie moze wejsc do pociagu
	printf("Pociag %d zamyka wejscia\n", getpid());
	semctl(semID, BAGAZ, SETVAL, 0);
        semctl(semID, ROWER, SETVAL, 0);
	printf("Pociag %d zamknal wejscia\n", getpid());

       	//zerowanie pamieci dzielonej
	sem_op(semID, SH, -1);
        printf("Kasowanie pamieci dzielonej\n");
	for(int i = 0; i < pasazerowieWpociagu + roweryWpociagu; i++) {
		sh[i] = 0;
	}
	sh[wielkoscPamieci - 5] = 0;
	sh[wielkoscPamieci - 4] = pasazerowieWpociagu;
	sh[wielkoscPamieci - 1] = 0;
	sem_op(semID, SH, 1);

	printf("Pociag %d opuszcza peron\n", getpid());
	printf("Pozostalo na peronie: %d\n", sh[ZAPISP]);
	sem_op(semID, POCIAG, 1);

	//pociag wroci na peron po czasie 'przyjazd';
	sleep(przyjazd);

	sem_op(semID, SH, -1);
	i = sh[ZAPISP];
	sem_op(semID, SH, 1);

    } while(i > 0);

    printf("Pociag %d konczy kursowac - brak pasazerow na peronie\n", getpid());
    exit(0);
}

int main(int argc, char *argv[]) {
    printf("-------------KIEROWNIK-----------------\n");

    //Sprawdzanie liczby wymaganych argumentow
    if (argc != 7) {
        perror("Brak wymaganych argumentów.\n");
        exit(1);
    }

    //Odbieranie argumentu i konwersja na int
    pociagi = atoi(argv[1]);
    wielkoscPamieci = atoi(argv[2]);
    odjazd = atoi(argv[3]);
    przyjazd = atoi(argv[4]);
    pasazerowieWpociagu = atoi(argv[5]);
    roweryWpociagu = atoi(argv[6]);

    //Wczytywanie pamieci dzielonej
    if((kluczM = ftok(".", 'M')) == -1) {
	printf("Blad ftok");
	exit(1);
    }
    shmID = shmID = shmget(kluczM, wielkoscPamieci * sizeof(int), IPC_CREAT | 0666);
    if(shmID == -1) {
	perror("Blad pamieci dzielonej");
	exit(1);
    }
    sh = (int*)shmat(shmID, NULL, 0);
    if (sh == (void *)-1) {
        perror("Błąd dołączania pamięci współdzielonej");
        exit(1);
    }

    //Tworzenie klucza dla semaforow
    if((kluczS = ftok(".", 'S')) == -1) {
	printf("Blad ftok");
	exit(1);
    }

    //Wczytywanie semaforow
    semID = semget(kluczS, 5, IPC_CREAT | 0666);
    if (semID == -1) {
        perror("Błąd semget");
        exit(1);
    }

    //Tworzenie pociagow
    printf("Tworzenie pociagow\n");
    for(int i = 0; i < pociagi; i++) {
        switch(fork()) {
	        case -1:
		    printf("Blad fork() (kierownik)");
		    exit(1);
		case 0:
		    pociag();
            	    exit(0);
	    }
    }

    //czekanie na zakonczenie procesow potomnych
    for (int i = 0; i < pociagi; i++) {
        wait(NULL);
    }

    printf("------KONIEC KIEROWNIK---------\n");

    exit(0);
}
