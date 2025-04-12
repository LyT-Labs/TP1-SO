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
int* board = NULL;
int my_id = -1;
int my_x = -1;
int my_y = -1;
int is_player_blocked = 0;
int was_player_moved = 1;
int error_sending_move = 0;

unsigned char last_dir = 0;

bool is_valid_movement(unsigned char dir) {
    int new_x = my_x;
    int new_y = my_y;

    // Calcular nueva posición según la dirección
    switch (dir) {
        case 0: new_y--; break;          // Arriba
        case 1: new_x++; new_y--; break; // Arriba-Derecha
        case 2: new_x++; break;          // Derecha
        case 3: new_x++; new_y++; break; // Abajo-Derecha
        case 4: new_y++; break;          // Abajo
        case 5: new_x--; new_y++; break; // Abajo-Izquierda
        case 6: new_x--; break;          // Izquierda
        case 7: new_x--; new_y--; break; // Arriba-Izquierda
        default: return false;             // Dirección inválida
    }

    // Verificar límites del tablero
    if (new_x < 0 || new_x >= width || new_y < 0 || new_y >= height) {
        #ifdef DEBUG
            fprintf(stderr, "[player] Movimiento fuera de límites: (%d, %d) -> (%d, %d)\n", my_x, my_y, new_x, new_y);
        #endif

        return false;
    }

    // Verificar si la celda está ocupada
    int index = new_y * width + new_x;
    if (board[index] <= 0) {
        #ifdef DEBUG
            fprintf(stderr, "[game_state] Jugador %d en (%d, %d) moviéndose a (%d, %d)\n", my_id, my_x, my_y, new_x, new_y);
            // Print all the squares in the area (9 squares)
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    int new_index = (my_y + i) * width + (my_x + j);
                    if (new_index >= 0 && new_index < width * height) {
                        fprintf(stderr, "[game_state] Celda (%d, %d): %d\n", my_y + j, my_y + i, board[new_index]);
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
        if (is_valid_movement(dir)) {
            return dir;
        }
    }

    return 0; // Si no hay movimientos válidos, retornar 0 (arriba)
}

unsigned char get_random_movement() {
    unsigned char dir;
    do {
        dir = rand() % 8; // Generar dirección aleatoria (0-7)
    } while (!is_valid_movement(dir));
    return dir;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s ancho alto\n", argv[0]);
        return 1;
    }

    width = atoi(argv[1]);
    height = atoi(argv[2]);

    int shm_fd = shm_open(SHM_STATE, O_RDONLY, 0);
    if (shm_fd < 0) {
        perror("[player] shm_open state");
        return 1;
    }
    int size = sizeof(GameState) + sizeof(int) * width * height;
    GameState* game_state = mmap(NULL, size, PROT_READ, MAP_SHARED, shm_fd, 0);

    int shm_sync_fd = shm_open(SHM_SYNC, O_RDWR, 0);
    if (shm_sync_fd < 0) {
        perror("[player] shm_open sync");
        return 1;
    }
    SyncState* sync = mmap(NULL, sizeof(SyncState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_sync_fd, 0);

    srand(getpid()); // Semilla para el generador de números aleatorios que no usamos lol

    // Inicializar el tablero
    board = malloc(sizeof(int) * width * height);
    if (board == NULL) {
        fprintf(stderr, "[player] Error al asignar memoria para el tablero\n");
        exit(1);
    }

    // buscar mi id
    for (int i = 0; i < game_state->player_count; i++) {
        if (game_state->players[i].pid == getpid()) {
            my_id = i;
            break;
        }
    }

    while (!game_state->is_finished) {
        // anti-inanición
        sem_wait(&sync->starvation_mutex);
        sem_post(&sync->starvation_mutex);
        
        // lightswitch enter
        sem_wait(&sync->reader_count_mutex);
        sync->reader_count++;
        if (sync->reader_count == 1) {
            sem_wait(&sync->game_state_mutex);
        }
        sem_post(&sync->reader_count_mutex);

        // copiar el tablero nuevo
        memcpy(board, game_state->board, sizeof(int) * width * height);

        is_player_blocked = game_state->players[my_id].is_blocked;

        // para después no moverse si no cambié de posición
        if (my_x == game_state->players[my_id].x && my_y == game_state->players[my_id].y) {
            was_player_moved = 0;
        } else {
            was_player_moved = 1;
        }
        
        my_x = game_state->players[my_id].x;
        my_y = game_state->players[my_id].y;
            
        // lightswitch exit (fin lectura)
        sem_wait(&sync->reader_count_mutex);
        sync->reader_count--;
        if (sync->reader_count == 0) {
            sem_post(&sync->game_state_mutex);
        }
        sem_post(&sync->reader_count_mutex);
        
        
        if (is_player_blocked) {
            break;
        }

        unsigned char dir = get_first_valid_movement();
        
        // Evita pedir moverse si no ha cambiado de posición
        // A menos que me ganaron el movimiento, por ende cambió la dirección
        // Excepto que sea porque el pipe estaba lleno, ahí sí se tiene que mover
        if (!was_player_moved && dir == last_dir && !error_sending_move) {
            continue;
        }
        error_sending_move = 0;
        

        // Validar si el pipe está listo para escritura
        struct pollfd pfd;
        pfd.fd = STDOUT_FILENO;
        pfd.events = POLLOUT;

        int ret = poll(&pfd, 1, 0); // Timeout de 0 ms para no bloquear
        if (ret == -1 || ret == 0) {
            #ifdef DEBUG
                fprintf(stderr, "[player] Error al verificar game_state del pipe con poll o el pipe está lleno\n");
            #endif

            error_sending_move = 1;
            continue;
        }

        // Enviar movimiento al pipe
        ret = write(STDOUT_FILENO, &dir, 1);
        if (ret == -1) {
            #ifdef DEBUG
                fprintf(stderr, "[player] Pipe lleno, no se puede escribir ahora\n");
            #endif

            error_sending_move = 1;
            continue;
        }
        last_dir = dir;

        
        // usleep(1000 * 1000);
    }

    free(board);
    
    #ifdef DEBUG
        fprintf(stderr, "[player] Terminado\n");
    #endif
    
    // cerrar memoria compartida
    if(munmap(game_state, size) == -1) {
        #ifdef DEBUG
            fprintf(stderr, "[player] Error al unmapear game_state: %s\n", strerror(errno));
        #endif
        exit(1);
    }
    if(munmap(sync, sizeof(SyncState)) == -1) {
        #ifdef DEBUG
            fprintf(stderr, "[player] Error al unmapear sync: %s\n", strerror(errno));
        #endif
        exit(1);
    }
    if(close(shm_fd) == -1) {
        #ifdef DEBUG
            fprintf(stderr, "[player] Error al cerrar shm_fd: %s\n", strerror(errno));
        #endif
        exit(1);
    }
    if(close(shm_sync_fd) == -1) {
        #ifdef DEBUG
            fprintf(stderr, "[player] Error al cerrar shm_sync_fd: %s\n", strerror(errno));
        #endif
        exit(1);
    }
    
    return 0;
    
}
