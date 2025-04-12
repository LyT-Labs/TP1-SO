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

// #define DEBUG

int width = 0;
int height = 0;
int* tablero = NULL;
int mi_id = -1;
int mi_x = -1;
int mi_y = -1;
int estoy_bloqueado = 0;
int me_movieron = 1;
int error_sending_move = 0;

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
        #ifdef DEBUG
            fprintf(stderr, "[player] Movimiento fuera de límites: (%d, %d) -> (%d, %d)\n", mi_x, mi_y, nuevo_x, nuevo_y);
        #endif

        return false;
    }

    // Verificar si la celda está ocupada
    int indice = nuevo_y * width + nuevo_x;
    if (tablero[indice] <= 0) {
        #ifdef DEBUG
            fprintf(stderr, "[Estado] Jugador %d en (%d, %d) moviéndose a (%d, %d)\n", mi_id, mi_x, mi_y, nuevo_x, nuevo_y);
            // Print all the squares in the area (9 squares)
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    int nuevo_indice = (mi_y + i) * width + (mi_x + j);
                    if (nuevo_indice >= 0 && nuevo_indice < width * height) {
                        fprintf(stderr, "[Estado] Celda (%d, %d): %d\n", mi_y + j, mi_y + i, tablero[nuevo_indice]);
                    }
                }
            }
        #endif

        return false;
    }

    return true;
}

unsigned char get_first_valid_movement() {
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

    srand(getpid()); // Semilla para el generador de números aleatorios que no usamos lol

    // Inicializar el tablero
    tablero = malloc(sizeof(int) * estado->width * estado->height);
    if (tablero == NULL) {
        fprintf(stderr, "[player] Error al asignar memoria para el tablero\n");
        exit(1);
    }

    // buscar mi id
    for (int i = 0; i < estado->cantidad_jugadores; i++) {
        if (estado->jugadores[i].pid == getpid()) {
            mi_id = i;
            break;
        }
    }

    while (!estado->terminado) {
        // anti-inanición
        sem_wait(&sync->C);
        sem_post(&sync->C);
        
        // lightswitch enter
        sem_wait(&sync->E);
        sync->F++;
        if (sync->F == 1) {
            sem_wait(&sync->D);
        }
        sem_post(&sync->E);

        // usleep(1000 * 1000); // 200ms

        width = estado->width;
        height = estado->height;

        // copiar el tablero nuevo
        memcpy(tablero, estado->tablero, sizeof(int) * estado->width * estado->height);

        estoy_bloqueado = estado->jugadores[mi_id].bloqueado;

        // para después no moverse si no cambié de posición
        if (mi_x == estado->jugadores[mi_id].x && mi_y == estado->jugadores[mi_id].y) {
            me_movieron = 0;
        } else {
            me_movieron = 1;
        }
        
        mi_x = estado->jugadores[mi_id].x;
        mi_y = estado->jugadores[mi_id].y;
            
        // lightswitch exit (fin lectura)
        sem_wait(&sync->E);
        sync->F--;
        if (sync->F == 0) {
            sem_post(&sync->D);
        }
        sem_post(&sync->E);
        
        
        if (estoy_bloqueado) {
            break;
        }
        
        // Evita pedir moverse si no ha cambiado de posición
        // Excepto que sea porque el pipe estaba lleno, ahí sí se tiene que mover
        if (!me_movieron && !error_sending_move) {
            continue;
        }
        error_sending_move = 0;
        
        // sem_post(&sync->C);
        // // generar movimiento random
        // unsigned char dir;
        // do {
        //     dir = rand() % 8; // Generar dirección aleatoria (0-7)
        // } while (!es_movimiento_valido(dir));
        
        unsigned char dir = get_first_valid_movement();

        // Validar si el pipe está listo para escritura
        struct pollfd pfd;
        pfd.fd = STDOUT_FILENO;
        pfd.events = POLLOUT;

        int ret = poll(&pfd, 1, 0); // Timeout de 0 ms para no bloquear
        if (ret == -1 || ret == 0) {
            #ifdef DEBUG
                fprintf(stderr, "[player] Error al verificar estado del pipe con poll o el pipe está lleno\n");
            #endif

            error_sending_move = 1;
            continue; // Saltar esta iteración del loop
        }

        // Enviar movimiento al pipe
        ret = write(STDOUT_FILENO, &dir, 1);
        if (ret == -1) {
            #ifdef DEBUG
                fprintf(stderr, "[player] Pipe lleno, no se puede escribir ahora\n");
            #endif

            error_sending_move = 1;
        }
        
        // usleep(1000 * 1000);
    }

    free(tablero);
    
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
