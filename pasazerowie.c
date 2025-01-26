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
#include <errno.h>
#include <string.h>

//nazwy semaforow
#define PERON 0
#define BAGAZ 1
#define ROWER 2
#define SH 4

//zmienne globalne
key_t kluczM, kluczS;
int* sh;
int semID, shmID, pasazerowie, wielkoscPamieci, zapisB, zapisR;

//indeksy przechowywujace miejsca do zapisu w pamieci dzielonej
#define ZAPISB sh[wielkoscPamieci - 5]
#define ZAPISR sh[wielkoscPamieci - 4]
#define ZAPISP wielkoscPamieci - 3
#define BLOKADA wielkoscPamieci - 2

//funkcja obslugujaca operacje na sempaforach
void sem_op(int sem_id, int sem_num, int op) {
    struct sembuf sops = {sem_num, op, 0};
    int wynik = semop(sem_id, &sops, 1);
    if (wynik == -1) {
	if(errno == EINTR) {
		sem_op(sem_id, sem_num, op);
	}
	else {
        	perror("Blad operacji na semaforze");
        	exit(1);
	}
    }
}

//funkcja obslugujaca sygnal wysylany z procesu ZAWIADOWCA
void obslugaSygnal2(int sig) {
	printf("\033[1;31mPERON ZAMKNIETY\033[0m\n");
	sem_op(semID, SH, -1);
	sh[BLOKADA] = 1;
	sem_op(semID, SH, 1);
}

//obsluga procesu pasazer
void pasazer() {
    //ustawianie losowej zmiennej - czy pasazer ma rower czy nie
    srand(getpid());
    int rower = rand() % 2;

    //informacja - pasazer o numerze PID zostal stworzony
    if(rower) {
        printf("\033[32mPasazer z rowerem zostal stworzony. NR %d\033[0m\n", getpid());
    }
    else {
        printf("\033[33mPasazer z bagazem zostal stworzony. NR %d\033[0m\n", getpid());
    }

    //pasazer wchodzi na peron jesli nie jest zablokowany
    sem_op(semID, PERON, -1);

    int blokada = 0;

    sem_op(semID, SH, -1);
    blokada = sh[BLOKADA];
    sem_op(semID, SH, 1);

    //jesli peron jest zablokowany proces sie konczy
    if(blokada == 1) {
	sem_op(semID, PERON, 1);
	exit(0);
    }

    //zwiekszanie licznika osob na peronie
    sem_op(semID, SH, -1);
    sh[ZAPISP]++;
    sem_op(semID, SH, 1);
    printf("pasazer na peronie. NR: %d\n", getpid());

    //pasazer ustawia sie do odpowiedniej kolejki w zalaznosci czy ma bagaz czy rower
    if(rower) {
        sem_op(semID, ROWER, -1);

	printf("\033[32mPasazer %d z ROWEREM w kolejce\033[0m\n", getpid());

	sem_op(semID, SH, -1);
	sh[ZAPISR] = getpid();
       	printf("\033[32mPasazer z ROWEREM wsiadl do pociagu. NR %d\033[0m\n", getpid());
        sh[wielkoscPamieci - 4]++;
        sh[ZAPISP]--;
        sem_op(semID, SH, 1);
     }
     else {
        sem_op(semID, BAGAZ, -1);

        printf("\033[33mPasazer %d z BAGAZEM w kolejce\033[0m\n", getpid());

	sem_op(semID, SH, -1);
	sh[ZAPISB] = getpid();
        printf("\033[33mPasazer z BAGAZEM wsiadl do pociagu. NR %d\033[0m\n", getpid());
	sh[wielkoscPamieci - 5]++;
	sh[ZAPISP]--;
	sem_op(semID, SH, 1);
     }

    //pasazer wychodzi z peronu
    sem_op(semID, PERON, 1);
    printf("pasazer %d wychodzi z peronu\n", getpid());

    exit(0);
}

int main(int argc, char *argv[]) {
    //obsluga sygnalu SIGUSR1 - zamkniecie peronu
    struct sigaction sigTermHandler;
    sigTermHandler.sa_handler = obslugaSygnal2;
    sigemptyset(&sigTermHandler.sa_mask);
    sigTermHandler.sa_flags = 0;
    sigaction(SIGUSR1, &sigTermHandler, NULL);

    printf("\033[1;31m----------PASAZEROWIE--------PID: %d\033[0m\n", getpid());

    //Sprawdzanie liczby wymaganych argumentow
    if (argc != 3) {
        fprintf(stderr, "Brak wymaganych argumentów.\n");
        exit(1);
    }

    //Odbieranie argumentu i konwersja na int
    pasazerowie = atoi(argv[1]);
    wielkoscPamieci = atoi(argv[2]);

    //Wczytywanie pamieci dzielonej
    if((kluczM = ftok(".", 'M')) == -1) {
	printf("Blad ftok");
	exit(1);
    }
    shmID = shmget(kluczM, wielkoscPamieci * sizeof(int), IPC_CREAT | 0666);
    if(shmID == -1) {
	perror("Blad pamieci dzielonej");
	exit(1);
    }
    sh = (int*)shmat(shmID, NULL, 0);
    if (sh == (int *)-1) {
        perror("Blad dolaczania pamieci wspoldzielonej");
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
        perror("Blad semget");
        exit(1);
    }

    //Tworzenie pasazerow
    for(int i = 0; i < pasazerowie; i++) {
	int pidP = fork();
        switch(pidP) {
	        case -1:
		    	perror("Blad fork() (pasazerowie)");
			if(errno == EAGAIN) {
				printf("Error code: %d (%s)\n", errno, strerror(errno));
			}
			exit(1);
		case 0:
		    	pasazer();
		    	exit(0);
	}
    }

    //czekanie na procesy potomne
    for(int i = 0; i < pasazerowie; i++) {
        wait(NULL);
    }
    printf("\033[1;31m------PASARZEROWIE KONIEC-------\033[0m\n");
    exit(0);
}
