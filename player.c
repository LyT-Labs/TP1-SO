#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include "game_state.h"
#include <signal.h>
#include <poll.h> // Incluir para usar poll()

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s ancho alto\n", argv[0]);
        return 1;
    }

    int ancho = atoi(argv[1]);
    int alto = atoi(argv[2]);

    int shm_fd = shm_open("/game_state", O_RDONLY, 0);
    if (shm_fd < 0) {
        perror("[player] shm_open state");
        return 1;
    }
    int size = sizeof(GameState) + sizeof(int) * ancho * alto;
    GameState* estado = mmap(NULL, size, PROT_READ, MAP_SHARED, shm_fd, 0);

    int shm_sync_fd = shm_open("/game_sync", O_RDWR, 0);
    if (shm_sync_fd < 0) {
        perror("[player] shm_open sync");
        return 1;
    }
    SyncState* sync = mmap(NULL, sizeof(SyncState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_sync_fd, 0);

    srand(getpid());

    while (!estado->terminado) {
        // protocolo de lector
        sem_wait(&sync->C);
        sem_post(&sync->C);
        
        
        sem_wait(&sync->E);
        sync->F++;
        if (sync->F == 1) {
            sem_wait(&sync->D);
        }
        sem_post(&sync->E);
        
        // leer estado del jugador
        int mi_id = -1;
        for (int i = 0; i < estado->cantidad_jugadores; i++) {
            if (estado->jugadores[i].pid == getpid()) {
                mi_id = i;
                break;
            }
            
        }

        if (mi_id == -1) {
            fprintf(stderr, "No encontré mi pid en el estado\n");
            break;
        }
        
        Player* yo = &estado->jugadores[mi_id];
        
        // fin lectura
        sem_wait(&sync->E);
        sync->F--;
        if (sync->F == 0) {
            sem_post(&sync->D);
        }
        sem_post(&sync->E);
        // sem_post(&sync->C);
        
        if (yo->bloqueado) {
            break;
        }
        // generar movimiento random
        unsigned char dir = rand() % 8;

        // Validar si el pipe está listo para escritura
        struct pollfd pfd;
        pfd.fd = STDOUT_FILENO;
        pfd.events = POLLOUT;

        int ret = poll(&pfd, 1, 0); // Timeout de 0 ms para no bloquear
        if (ret == -1) {
            perror("[player] Error al verificar estado del pipe con poll");
            break;
        } else if (ret == 0) {
            // fprintf(stderr, "[player] Pipe sobrecargado, no se puede escribir ahora\n");
            // Opcional: puedes agregar un pequeño retraso antes de reintentar
            // usleep(1000); // 1ms
            continue; // Saltar esta iteración del loop
        }

        // Intentar escribir si el pipe está listo
        if (write(STDOUT_FILENO, &dir, 1) == -1) {
            perror("[player] Error al escribir movimiento");
            exit(1);
        }
        
        // Tipo de espera con sleep
        // usleep(200 * 1000); // 20ms
        
    }
    
    fprintf(stderr, "[player] Terminado\n");
    // cerrar memoria compartida
    if(munmap(estado, size) == -1) {
        fprintf(stderr, "[player] Error al unmapear estado\n");
        exit(1);
    }
    if(munmap(sync, sizeof(SyncState)) == -1) {
        fprintf(stderr, "[player] Error al unmapear sync\n");
        exit(1);
    }
    if(close(shm_fd) == -1) {
        fprintf(stderr, "[player] Error al cerrar shm_fd\n");
        exit(1);
    }
    if(close(shm_sync_fd) == -1) {
        fprintf(stderr, "[player] Error al cerrar shm_sync_fd\n");
        exit(1);
    }
    
    return 0;
    
}
