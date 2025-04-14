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
#include <limits.h>

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

unsigned char get_best_movement() {
    int best_value = -1; // Valor más alto encontrado
    unsigned char best_dir = 0; // Dirección hacia la mejor celda
    int best_distance = width * height; // Distancia mínima a la mejor celda

    // Recorrer todo el tablero
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = y * width + x;

            // Ignorar celdas ocupadas o con valores no positivos
            if (board[index] <= 0) {
                continue;
            }

            // Calcular la distancia de Manhattan desde la posición actual
            int distance = abs(my_x - x) + abs(my_y - y);

            // Priorizar celdas con valores más altos y distancias más cortas
            if (board[index] > best_value || (board[index] == best_value && distance < best_distance)) {
                best_value = board[index];
                best_distance = distance;

                // Determinar la dirección hacia la celda
                if (x > my_x && y == my_y) best_dir = 2; // Derecha
                else if (x < my_x && y == my_y) best_dir = 6; // Izquierda
                else if (x == my_x && y > my_y) best_dir = 4; // Abajo
                else if (x == my_x && y < my_y) best_dir = 0; // Arriba
                else if (x > my_x && y > my_y) best_dir = 3; // Abajo-Derecha
                else if (x > my_x && y < my_y) best_dir = 1; // Arriba-Derecha
                else if (x < my_x && y > my_y) best_dir = 5; // Abajo-Izquierda
                else if (x < my_x && y < my_y) best_dir = 7; // Arriba-Izquierda
            }
        }
    }

    // Validar si la dirección encontrada es válida
    if (is_valid_movement(best_dir)) {
        return best_dir;
    }

    // Si no se encuentra una dirección válida, retornar un movimiento aleatorio
    return get_random_movement();
}


#define DIRECCIONES 8
#define PROFUNDIDAD_MAX 1000

const int dx[DIRECCIONES] = {  0,  1, 1, 1, 0, -1, -1, -1 };
const int dy[DIRECCIONES] = { -1, -1, 0, 1, 1,  1,  0, -1 };

int en_rango(int x, int y, int w, int h) {
    return x >= 0 && y >= 0 && x < w && y < h;
}

int recompensa_en(int* tablero, int x, int y, int w, int h) {
    if (!en_rango(x, y, w, h)) return 0;
    int val = tablero[y * w + x];
    return val > 0 ? val : 0;
}

bool esta_libre(int* tablero, int x, int y, int w, int h) {
    return en_rango(x, y, w, h) && tablero[y * w + x] > 0;
}

int distancia_total_a_otros(GameState* estado, int x, int y, int my_id) {
    int total = 0;
    for (int i = 0; i < estado->player_count; i++) {
        if (i == my_id || estado->players[i].is_blocked) continue;
        int dx = x - estado->players[i].x;
        int dy = y - estado->players[i].y;
        total += dx * dx + dy * dy;
    }
    return total;
}

int area_explorable(int* tablero, int* visitado, int x, int y, int w, int h, int profundidad) {
    if (profundidad <= 0 || !esta_libre(tablero, x, y, w, h) || visitado[y * w + x]) return 0;
    visitado[y * w + x] = 1;
    int total = 1;
    for (int i = 0; i < DIRECCIONES; i++) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        total += area_explorable(tablero, visitado, nx, ny, w, h, profundidad - 1);
    }
    return total;
}

int bfs_camino(int* tablero, GameState* estado, int* visitado, int x, int y, int w, int h, int profundidad, int my_id, int acumulado) {
    int max_score = acumulado;
    int queue[w * h][3]; // Queue to store {x, y, depth}
    int front = 0, rear = 0;

    queue[rear][0] = x;
    queue[rear][1] = y;
    queue[rear][2] = profundidad;
    rear++;
    visitado[y * w + x] = 1;

    while (front < rear) {
        int cx = queue[front][0];
        int cy = queue[front][1];
        int depth = queue[front][2];
        front++;

        max_score += recompensa_en(tablero, cx, cy, w, h);

        if (depth <= 0) continue;

        for (int i = 0; i < DIRECCIONES; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (esta_libre(tablero, nx, ny, w, h) && !visitado[ny * w + nx]) {
                visitado[ny * w + nx] = 1;
                queue[rear][0] = nx;
                queue[rear][1] = ny;
                queue[rear][2] = depth - 1;
                rear++;
            }
        }
    }

    return max_score;
}

unsigned char ia_god_get_movement(GameState* estado, int* tablero, int my_id, int my_x, int my_y, int w, int h) {
    int mejor_score = INT_MIN;
    unsigned char mejor_dir = 255;

    for (unsigned char dir = 0; dir < DIRECCIONES; dir++) {
        dir = (dir + 2) % 8; // Cambia la dirección para que sea más fácil de entender
        int nx = my_x + dx[dir];
        int ny = my_y + dy[dir];
        if (!esta_libre(tablero, nx, ny, w, h)) continue;

        int recompensa = recompensa_en(tablero, nx, ny, w, h);

        int visitado[w * h];
        memset(visitado, 0, sizeof(visitado));
        int score = bfs_camino(tablero, estado, visitado, nx, ny, w, h, PROFUNDIDAD_MAX, my_id, 0);

        memset(visitado, 0, sizeof(visitado));
        // int explorables = area_explorable(tablero, visitado, nx, ny, w, h, PROFUNDIDAD_MAX);
        // if (explorables < 20) score -= 1000; // encerrado, penaliza brutalmente

        // int distancia = distancia_total_a_otros(estado, nx, ny, my_id);
        // score += distancia; // alejarse de otros

        if (score > mejor_score) {
            mejor_score = score;
            mejor_dir = dir;
        }
    }

    return mejor_dir;
}
// int distancia(int x1, int y1, int x2, int y2) {
//     int dx = x2 - x1;
//     int dy = y2 - y1;
//     return dx * dx + dy * dy;
// }

// unsigned char direccion_hacia(int x1, int y1, int x2, int y2) {
//     if (x2 > x1 && y2 < y1) return 1;
//     if (x2 > x1 && y2 == y1) return 2;
//     if (x2 > x1 && y2 > y1) return 3;
//     if (x2 == x1 && y2 > y1) return 4;
//     if (x2 < x1 && y2 > y1) return 5;
//     if (x2 < x1 && y2 == y1) return 6;
//     if (x2 < x1 && y2 < y1) return 7;
//     if (x2 == x1 && y2 < y1) return 0;
//     return 255;
// }
// int contar_libres_en_linea(int* tablero, int x, int y, int dx, int dy, int w, int h) {
//     int libres = 0;
//     for (int i = 1; i < 10; i++) {
//         int nx = x + i * dx;
//         int ny = y + i * dy;
//         if (!esta_libre(tablero, nx, ny, w, h)) break;
//         libres++;
//     }
//     return libres;
// }

// unsigned char ia_esquina_exploradora(GameState* estado, int* tablero, int my_id, int my_x, int my_y, int w, int h) {
//     // Paso 1: ir a la esquina más cercana
//     int esquinas[4][2] = { {0, 0}, {w - 1, 0}, {w - 1, h - 1}, {0, h - 1} };
//     int min_dist = INT_MAX;
//     int* mejor_esquina = NULL;

//     for (int i = 0; i < 4; i++) {
//         int d = distancia(my_x, my_y, esquinas[i][0], esquinas[i][1]);
//         if (d < min_dist) {
//             min_dist = d;
//             mejor_esquina = esquinas[i];
//         }
//     }

//     int destino_x = mejor_esquina[0];
//     int destino_y = mejor_esquina[1];

//     if (my_x != destino_x || my_y != destino_y) {
//         unsigned char dir = direccion_hacia(my_x, my_y, destino_x, destino_y);
//         int nx = my_x + dx[dir];
//         int ny = my_y + dy[dir];
//         if (esta_libre(tablero, nx, ny, w, h)) return dir;
//     }

//     // Paso 2: elegir dirección con más celdas libres rectas
//     int max_libres = -1;
//     unsigned char mejor_dir = 255;
//     int dir_candidatas[] = { 2, 4, 6, 0 }; // derecha, abajo, izquierda, arriba

//     for (int i = 0; i < 4; i++) {
//         int dir = dir_candidatas[i];
//         int libres = contar_libres_en_linea(tablero, my_x, my_y, dx[dir], dy[dir], w, h);
//         if (libres > max_libres) {
//             max_libres = libres;
//             mejor_dir = dir;
//         }
//     }

//     // Intentar ir en la dirección más libre
//     int nx = my_x + dx[mejor_dir];
//     int ny = my_y + dy[mejor_dir];
//     if (esta_libre(tablero, nx, ny, w, h)) return mejor_dir;

//     // Buscar alternativa
//     for (unsigned char d = 0; d < DIRECCIONES; d++) {
//         nx = my_x + dx[d];
//         ny = my_y + dy[d];
//         if (esta_libre(tablero, nx, ny, w, h)) return d;
//     }

//     return 255;
// }


// // 0: →, 1: ↓, 2: ←, 3: ↑
// const int espiral_dx[] = {1, 0, -1, 0};
// const int espiral_dy[] = {0, 1, 0, -1};

// unsigned char mover_hacia(int x1, int y1, int x2, int y2) {
//     if (x2 > x1 && y2 == y1) return 2;
//     if (x2 < x1 && y2 == y1) return 6;
//     if (x2 == x1 && y2 > y1) return 4;
//     if (x2 == x1 && y2 < y1) return 0;
//     if (x2 > x1 && y2 > y1) return 3;
//     if (x2 > x1 && y2 < y1) return 1;
//     if (x2 < x1 && y2 > y1) return 5;
//     if (x2 < x1 && y2 < y1) return 7;
//     return 255;
// }

// unsigned char ia_espiral(GameState* estado, int* tablero, int my_id, int my_x, int my_y, int w, int h) {
//     static int dir = 0;  // dirección actual (0=→, 1=↓, 2=←, 3=↑)
//     static int pasos = 1; // pasos a dar en esa dirección
//     static int contador = 0; // pasos dados
//     static int veces = 0; // cuántas veces se repitió una dirección

//     int nx = my_x + espiral_dx[dir];
//     int ny = my_y + espiral_dy[dir];

//     if (esta_libre(tablero, nx, ny, w, h)) {
//         contador++;
//         if (contador >= pasos) {
//             contador = 0;
//             dir = (dir + 1) % 4;
//             veces++;
//             if (veces == 2) {
//                 pasos++;
//                 veces = 0;
//             }
//         }
//         return mover_hacia(my_x, my_y, nx, ny);
//     }

//     // si está bloqueado, buscar movimiento alternativo válido
//     for (unsigned char d = 0; d < DIRECCIONES; d++) {
//         int ax = my_x + dx[d];
//         int ay = my_y + dy[d];
//         if (esta_libre(tablero, ax, ay, w, h)) return d;
//     }

//     return 255;
// }


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

        unsigned char dir = ia_god_get_movement(game_state, board, my_id, my_x, my_y, width, height);
        
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
