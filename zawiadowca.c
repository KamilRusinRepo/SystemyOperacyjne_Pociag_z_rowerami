#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <sys/resource.h>

//nazwy semaforow
#define PERON 0
#define BAGAZ 1
#define ROWER 2
#define POCIAG 3
#define SH 4

//zmienne globalne
int semID, shmID, pozostaleProcesy;
int pidK, pidP, wielkoscPamieci;
int* sh;

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

//funkcja sluzaca do pobierania maksymanlnej liczby procesow dla uzytkownika
int maksymalnieProcesow() {
struct rlimit rlim;

    if (getrlimit(RLIMIT_NPROC, &rlim) == -1) {
        perror("getrlimit");
        return 1;
    }

    return rlim.rlim_cur;
}

//funkcja sluzy do sprawdzenia ile uzytkownik ma wlaczonych procesow
int obecnaLiczbaProcesow() {
    char command[256];
    snprintf(command, sizeof(command), "ps -u %s --no-headers | wc -l", getenv("USER"));
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("Blad przy odczycie obecnej liczby procesow");
        return -1;
    }
    int count;
    fscanf(fp, "%d", &count);
    fclose(fp);
    return count;
}
//wysyla sygnal do procesu pociag by odjechal z peronu
void sygnalDoPociagu(int sig) {
	printf("WYSYLAM SYGNAL DO POCIAGU\n");
	if(sh[wielkoscPamieci - 1] == 0) {
	    printf("BRAK POCIAGU NA PERONIE\n");
	}
	else if (kill(sh[wielkoscPamieci - 1], SIGUSR2) == -1) {
            perror("Blad przy wysylaniu SIGUSR2");
        }
        else {
            printf("Sygnal SIGUSR2 wyslany do procesu %d\n", sh[wielkoscPamieci - 1]);
        }
}

//wysyla sygnal do procesu pasazerowie o zamknieciu peronu
void sygnalDoPasazerowie(int sig) {
	printf("WYSYLANIE SYGNALU DO PASAZEROWIE\n");
	if (kill(pidP, SIGUSR1) == -1) {
            perror("Blad przy wysylaniu SIGUSR1");
        }
        else {
            printf("Sygnal SIGUSR1 wyslany do procesu %d\n", pidP);
        }
}

//funkcja usuwajaca pamiec dzielona i semafory po obsluzeniu sygnalu sigint
void koniecSygnal(int sig) {
        shmctl(shmID, IPC_RMID, NULL);
	semctl(semID, PERON, IPC_RMID, NULL);
    	semctl(semID, BAGAZ, IPC_RMID, NULL);
	semctl(semID, ROWER, IPC_RMID, NULL);
	semctl(semID, POCIAG, IPC_RMID, NULL);
	semctl(semID, SH, IPC_RMID, NULL);
	printf("Zakonczono przez sygnal %d\n", sig);
	exit(1);
}

//funkcja usuwa pamiec dzielona i semafory gdy program zakonczy sie bez bledow
void czyszczenie() {
	printf("CZYSZCZENIE\n");
	semctl(semID, PERON, IPC_RMID, NULL);
        semctl(semID, BAGAZ, IPC_RMID, NULL);
	semctl(semID, ROWER, IPC_RMID, NULL);
	semctl(semID, POCIAG, IPC_RMID, NULL);
	semctl(semID, SH, IPC_RMID, NULL);
	if (shmctl(shmID, IPC_RMID, NULL) == -1) {
        	perror("Blad przy usuwaniu pamieci dzielonej");
        	exit(1);
    	}
}

//funkcja sprawdzajaca poprawnosc wprowadzanych danych
int wczytaj_dane(int type) {
	char buf[10];
	int liczba;

	while(1) {
		if (fgets(buf, sizeof(buf), stdin) != NULL) {
			if (sscanf(buf, "%d", &liczba) == 1) { // Konwersja tekstu na liczbe
				if(type == 0 && liczba > pozostaleProcesy) {
					printf("Liczba przekracza mozliwa liczbe procesow do utworzenia\n");
				}
				//else if(type == 1 && liczba > 3200) {
				//	printf("Liczba przekracza maksymalna liczbe jaka moze przyjac semafor\n");
				//}
				else if(liczba >= 0) {
					if(type == 0) {
						pozostaleProcesy -= liczba;
						printf("Pozostalo procesow: %d\n", pozostaleProcesy);
					}
					printf("Wczytano: %d\n", liczba);
					return liczba;
				}
				else {
					printf("Liczba musi byc wieksza od 0\n");
				}
				printf("Podaj liczbe: ");
			}
			else {
				printf("Podaj liczbe jeszcze raz!\n");
			}
		}
		else {
        		printf("Blad odczytu!\n");
    		}
	}
}



int main() {
	//inicjalizacja zmiennych
	int pasazerowie, pociagi, pasazerowieWpociagu, roweryWpociagu, odjazd, przyjazd, peron, maxProcesy, obecneProcesy;
        key_t kluczM, kluczS;

	struct sigaction sigIntHandler, sigUsr1Handler, sigUsr2Handler;
        sigIntHandler.sa_handler = koniecSygnal;
        sigemptyset(&sigIntHandler.sa_mask);
        sigIntHandler.sa_flags = 0;
        sigaction(SIGINT, &sigIntHandler, NULL);

        sigUsr1Handler.sa_handler = sygnalDoPasazerowie;
        sigemptyset(&sigUsr1Handler.sa_mask);
        sigUsr1Handler.sa_flags = SA_RESTART;
        sigaction(SIGUSR1, &sigUsr1Handler, NULL);

        sigUsr2Handler.sa_handler = sygnalDoPociagu;
        sigemptyset(&sigUsr2Handler.sa_mask);
        sigUsr2Handler.sa_flags = SA_RESTART;
        sigaction(SIGUSR2, &sigUsr2Handler, NULL);

	printf("-----------ZAWIADOWCA---------------PID: %d\n", getpid());

	//sprawdzanie limitow procesow
	maxProcesy = maksymalnieProcesow();
	obecneProcesy = obecnaLiczbaProcesow();
	pozostaleProcesy = maxProcesy - obecneProcesy - 2;
	printf("Pozostalo %d procesow do wykorzystania\n", pozostaleProcesy);

	//wczytywanie danych
	printf("Podaj liczbe pasazerow: ");
	pasazerowie = wczytaj_dane(0);

	printf("Podaj liczbe pociagow: ");
	pociagi = wczytaj_dane(0);

	printf("Podaj liczbe miejsc w pociagu dla pasazerow bez roweru: ");
	pasazerowieWpociagu = wczytaj_dane(1);

	printf("Podaj liczbe miejsc w pociagu dla  pasazerow z rowerami: ");
	roweryWpociagu = wczytaj_dane(1);

	printf("Podaj czas stania pociagu na peronie: ");
	odjazd = wczytaj_dane(2);

	printf("Podaj czas trwania trasy pociagu: ");
	przyjazd = wczytaj_dane(2);

        printf("Podaj wielkosc peronu: ");
	peron = wczytaj_dane(1);

	//obliczanie wielkosci pamieci dzielonej
	wielkoscPamieci = pasazerowieWpociagu + roweryWpociagu + 5;

	//tworzenie klucza do pamieci dzielonej
	if((kluczM = ftok(".", 'M')) == -1) {
		printf("Blad ftok");
		exit(1);
	}

	//tworzenie pamieci dzielonej
        shmID = shmget(kluczM, wielkoscPamieci * sizeof(int), IPC_CREAT | IPC_EXCL | 0666); 
	if(shmID == -1) {
		perror("Blad pamieci dzielonej");
		exit(1);
	}

	//dodawanie znacznikow do pamieci dzielonej
	sh = (int*)shmat(shmID, NULL, 0);
	sh[wielkoscPamieci - 5] = 0;	//poczatkowy indeks zapisu dla pasazera z bagazem
	sh[wielkoscPamieci - 4] = pasazerowieWpociagu;	//poczatkowy indeks zapisu dla pasazera z rowerem
	sh[wielkoscPamieci - 3] = 0;	//poczatkowa ilosc pasazerow do rozwiezienia
	sh[wielkoscPamieci - 2] = 0;	//wartosc do blokowania peronu
	sh[wielkoscPamieci - 1] = 0;	//PID pociagu ktory znajduje sie na peronie

	//tworzenie klucza dla semaforow
	if((kluczS = ftok(".", 'S')) == -1) {
		printf("Blad ftok");
		exit(1);
	}

	//tworzenie semaforow
	semID = semget(kluczS, 5, IPC_CREAT | IPC_EXCL | 0666);

	//ustawianie poczatkowych wartosci w semaforach
	semctl(semID, PERON, SETVAL, peron);
	semctl(semID, POCIAG, SETVAL, 1);
	semctl(semID, SH, SETVAL, 1);
	semctl(semID, BAGAZ, SETVAL, 0);
	semctl(semID, ROWER, SETVAL, 0);

	//konwertowanie zmiennych do przekazania procesom potomnym
    	char pasazerowie_str[20];
    	snprintf(pasazerowie_str, sizeof(pasazerowie_str), "%d", pasazerowie);
    	char wielkoscPamieci_str[20];
    	snprintf(wielkoscPamieci_str, sizeof(wielkoscPamieci_str), "%d", wielkoscPamieci);
    	char pociagi_str[20];
    	snprintf(pociagi_str, sizeof(pociagi_str), "%d", pociagi);
    	char odjazd_str[20];
    	snprintf(odjazd_str, sizeof(odjazd_str), "%d", odjazd);
    	char przyjazd_str[20];
    	snprintf(przyjazd_str, sizeof(przyjazd_str), "%d", przyjazd);
    	char pasazerowieWpociagu_str[20];
    	snprintf(pasazerowieWpociagu_str, sizeof(pasazerowieWpociagu_str), "%d", pasazerowieWpociagu);
    	char roweryWpociagu_str[20];
    	snprintf(roweryWpociagu_str, sizeof(roweryWpociagu_str), "%d", roweryWpociagu);

    	printf("Tworzenie procesow\n");

	//tworzenie procesu pasazerowie
   	pidP = fork();
	switch(pidP) {
		case -1:
			printf("Blad fork() (zawiadowca)");
			exit(1);
		case 0:
			if(execl("./pasazerowie", "pasazerowie", pasazerowie_str, wielkoscPamieci_str, NULL) == -1) {
				perror("Blad tworzenia pasarzerowie");
				exit(1);
			}
			exit(0);
	}

    	//tworzenie procesu kierownik
	pidK = fork();
    	switch(pidK) {
		case -1:
			printf("Blad fork() (zawiadowca)");
			exit(1);
		case 0:
			if(execl("./kierownik", "kierownik", pociagi_str, wielkoscPamieci_str, odjazd_str, przyjazd_str, pasazerowieWpociagu_str, roweryWpociagu_str, NULL) == -1) {
				perror("Blad tworzenia kierownik");
				exit(1);
			}
			exit(0);
        }

    	//proces czeka na zakonczenie procesow potomnych
	waitpid(pidP, NULL, 0);
	printf("ZAWIADOWCA ZAKONCZYL CZEKANIE NA PROCES PASAZEROWIE\n");
	waitpid(pidK, NULL, 0);
	printf("ZAWIADOWCA ZAKONCZYL CZEKANIE NA PROCES KIEROWNIK\n");

	printf("------KONIEC ZAWIADOWCA----\n");

    	//wywolanie funkcji zwalniajacej pamiec i semafory
    	czyszczenie();
	exit(0);
}
