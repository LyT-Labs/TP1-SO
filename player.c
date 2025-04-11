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
#include <string.h>

int width = 0;
int height = 0;
int* tablero = NULL;
int mi_id = -1;
int mi_x = -1;
int mi_y = -1;
int estoy_bloqueado = 0;

bool es_movimiento_valido(unsigned char dir) {
    int nuevo_x = mi_x;
    int nuevo_y = mi_y;

    // Calcular nueva posición según la dirección
    switch (dir) {
        case 0: nuevo_y--; break;          // Arriba
        case 1: nuevo_x++; nuevo_y--; break; // Arriba-Derecha
        case 2: nuevo_x++; break;          // Derecha
        case 3: nuevo_x++; nuevo_y++; break; // Abajo-Derecha
        case 4: nuevo_y++; break;          // Abajo
        case 5: nuevo_x--; nuevo_y++; break; // Abajo-Izquierda
        case 6: nuevo_x--; break;          // Izquierda
        case 7: nuevo_x--; nuevo_y--; break; // Arriba-Izquierda
        default: return false;             // Dirección inválida
    }

    // Verificar límites del tablero
    if (nuevo_x < 0 || nuevo_x >= width || nuevo_y < 0 || nuevo_y >= height) {
        // fprintf(stderr, "[player] Movimiento fuera de límites: (%d, %d) -> (%d, %d)\n", mi_x, mi_y, nuevo_x, nuevo_y);
        return false;
    }

    // Verificar si la celda está ocupada
    int indice = nuevo_y * width + nuevo_x;
    if (tablero[indice] <= 0) {
        // Print estado
        // fprintf(stderr, "[Estado] Jugador %d en (%d, %d) moviéndose a (%d, %d)\n", tablero[indice], mi_x, mi_y, nuevo_x, nuevo_y);
        // // Print all the squares in the area (9 squares)
        // for (int i = -1; i <= 1; i++) {
        //     for (int j = -1; j <= 1; j++) {
        //         int nuevo_indice = (nuevo_y + i) * width + (nuevo_x + j);
        //         if (nuevo_indice >= 0 && nuevo_indice < width * height) {
        //             fprintf(stderr, "[Estado] Celda (%d, %d): %d\n", nuevo_x + j, nuevo_y + i, tablero[nuevo_indice]);
        //         }
        //     }
        // }

        return false;
    }

    return true;
}


unsigned char get_first_valid_movement()
{
    // Intentar mover en las 8 direcciones
    for (unsigned char dir = 0; dir < 8; dir++) {
        if (es_movimiento_valido(dir)) {
            return dir;
        }
    }

    return 0; // Si no hay movimientos válidos, retornar 0 (arriba)
}
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
        width = estado->width;
        height = estado->height;

        // copiar el tablero
        tablero = malloc(sizeof(int) * estado->width * estado->height);
        if (tablero == NULL) {
            fprintf(stderr, "[player] Error al asignar memoria para el tablero\n");
            exit(1);
        }
        memcpy(tablero, estado->tablero, sizeof(int) * estado->width * estado->height);

        for (int i = 0; i < estado->cantidad_jugadores; i++) {
            if (estado->jugadores[i].pid == getpid()) {
                mi_id = i;
                mi_x = estado->jugadores[i].x;
                mi_y = estado->jugadores[i].y;
                estoy_bloqueado = estado->jugadores[i].bloqueado;
                break;
            }
        }

        // if (copia.mi_id == -1) {
        //     fprintf(stderr, "No encontré mi pid en el estado\n");
        //     exit(1);
        //     break;
        // }
        
        
        // fin lectura
        sem_wait(&sync->E);
        sync->F--;
        if (sync->F == 0) {
            sem_post(&sync->D);
        }
        sem_post(&sync->E);
        // sem_post(&sync->C);
        
        if (estoy_bloqueado) {
            break;
        }
        // generar movimiento random
        unsigned char dir = rand() % 8;
        // unsigned char dir;
        // do {
        //     dir = rand() % 8; // Generar dirección aleatoria (0-7)
        //     // fprintf(stderr, "[player %d] Generando movimiento aleatorio: %d\n", copia.mi_id, dir);
        // } while (!es_movimiento_valido(dir));
        // unsigned char dir = get_first_valid_movement();


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
        // usleep(200 * 10000); // 20ms
        
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
