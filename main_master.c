// main_master.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>
#include "game_state.h"
#include <sys/select.h>

#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"

#define WIDTH_MIN 10
#define HEIGHT_MIN 10

#define WIDTH_MAX 100
#define HEIGHT_MAX 100

#define MAX_PLAYERS 9
#define MAX_NAME 16

#define WIDTH_DEFAULT 10
#define HEIGHT_DEFAULT 10
#define DELAY_DEFAULT 200
#define TIMEOUT_DEFAULT 10
#define SEED_DEFAULT time(NULL)
#define VIEW_DEFAULT NULL


unsigned short width;
unsigned short height;
unsigned int delay;
unsigned int timeout;
unsigned int seed;
unsigned int player_count;
char* view = NULL;
char* player_paths[MAX_PLAYERS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

unsigned int player_count = 0;


typedef struct {
    pid_t pid;
    int pipe_read_fd;  // máster lee de acá
    bool bloqueado;
} PlayerProc;

PlayerProc procesos[MAX_PLAYERS];

/*
A continuación se listan los parámetros que acepta el máster. Los parámetros entre
corchetes son opcionales y tienen un valor por defecto.

[-w width]: Ancho del tablero. Default y mínimo: 10
[-h height]: Alto del tablero. Default y mínimo: 10
[-d delay]: milisegundos que espera el máster cada vez que se imprime el estado. Default: 200
[-t timeout]: Timeout en segundos para recibir solicitudes de movimientos válidos. Default: 10
[-s seed]: Semilla utilizada para la generación del tablero. Default: time(NULL)
[-v view]: Ruta del binario de la vista. Default: Sin vista.
-p player1 player2: Ruta/s de los binarios de los jugadores. Mínimo: 1, Máximo: 9.

*/
void validate_args(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [-w width] [-h height] [-d delay] [-t timeout] [-s seed] [-v view] [-p player1 player2 ...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Inicializar valores por defecto
    width = WIDTH_DEFAULT;
    height = HEIGHT_DEFAULT;
    delay = DELAY_DEFAULT;
    timeout = TIMEOUT_DEFAULT;
    seed = SEED_DEFAULT;

    // Procesar argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delay = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            view = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0) {
            while (i + 1 < argc && player_count < MAX_PLAYERS) {
                player_paths[player_count++] = argv[++i];
            }
        } else {
            fprintf(stderr, "Parámetro desconocido: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }

    // Validar parámetros
    if (width < WIDTH_MIN || width > WIDTH_MAX) {
        fprintf(stderr, "El ancho del tablero debe estar entre %d y %d\n", WIDTH_MIN, WIDTH_MAX);
        exit(EXIT_FAILURE);
    }
    if (height < HEIGHT_MIN || height > HEIGHT_MAX) {
        fprintf(stderr, "El alto del tablero debe estar entre %d y %d\n", HEIGHT_MIN, HEIGHT_MAX);
        exit(EXIT_FAILURE);
    }
    if (player_count == 0) {
        fprintf(stderr, "Debe haber al menos un jugador especificado con -p\n");
        exit(EXIT_FAILURE);
    }
}

void init_game_state(GameState* state) {
    state->width = width;
    state->height = height;
    state->player_count = player_count;
    state->is_finished = false;

    // Inicializar jugadores
    for (int i = 0; i < player_count; i++) {
        state->players[i].score = 0;
        state->players[i].invalid_moves = 0;
        state->players[i].valid_moves = 0;
        state->players[i].is_blocked = false;
        state->players[i].pid = -1;
        // path/to/player => name = player
        char* last_slash = strrchr(player_paths[i], '/');
        if (last_slash != NULL) {
            strncpy(state->players[i].name, last_slash + 1, MAX_NAME - 1);
        } else {
            strncpy(state->players[i].name, player_paths[i], MAX_NAME - 1);
        }
        state->players[i].name[MAX_NAME - 1] = '\0'; // Asegurarse de que la cadena esté terminada

        // TODO: No deberia ser aleatorio. DEBE SER DETERMINISTICO
        state->players[i].x = rand() % width; // Posición aleatoria
        state->players[i].y = rand() % height; // Posición aleatoria

    }
    // Inicializar el tablero
    srand(seed);
    for (int i = 0; i < width * height; i++) {
        state->board[i] = (rand() % 9) + 1; // Valores aleatorios entre 1 y 9
    }
}

void init_sync_state(SyncState* sync) {
    sem_init(&sync->changes_available, 1, 0);
    sem_init(&sync->print_done, 1, 0); 
    sem_init(&sync->starvation_mutex, 1, 1);
    sem_init(&sync->game_state_mutex, 1, 0);
    sem_init(&sync->reader_count_mutex, 1, 1);
    sync->reader_count = 0;
}


void create_players(GameState* state) {
    for (int i = 0; i < player_count; i++) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("pipe");
            exit(1);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            // Proceso jugador
            close(pipefd[0]); // Cierra lectura
            dup2(pipefd[1], STDOUT_FILENO); // Redirige stdout al pipe
            close(pipefd[1]);

            setgid(1000); // Cambia el grupo del proceso

            char ancho_str[8], alto_str[8];
            snprintf(ancho_str, sizeof(ancho_str), "%hu", width);
            snprintf(alto_str, sizeof(alto_str), "%hu", height);
            execl(player_paths[i], player_paths[i], ancho_str, alto_str, NULL);
            perror("execl jugador");
            exit(1);
        } else {
            // Proceso máster
            close(pipefd[1]); // Cierra escritura
            procesos[i].pid = pid;
            procesos[i].pipe_read_fd = pipefd[0];
            procesos[i].bloqueado = false;
            state->players[i].pid = pid;
        }
    }
}

void create_view() {
    if (!view) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork vista");
        exit(1);
    }

    if (pid == 0) {
        setgid(1000); // Cambia el grupo del proceso

        char ancho_str[8], alto_str[8];
        snprintf(ancho_str, sizeof(ancho_str), "%hu", width);
        snprintf(alto_str, sizeof(alto_str), "%hu", height);
        execl(view, view, ancho_str, alto_str, NULL);
        perror("execl vista");
        exit(1);
    }
}


void modify_x_y_acording_to_dir(unsigned char dir, int* x, int* y) {
    switch (dir) {
        case 0: (*y)--; break;          // Arriba
        case 1: (*x)++; (*y)--; break; // Arriba-Derecha
        case 2: (*x)++; break;          // Derecha
        case 3: (*x)++; (*y)++; break; // Abajo-Derecha
        case 4: (*y)++; break;          // Abajo
        case 5: (*x)--; (*y)++; break; // Abajo-Izquierda
        case 6: (*x)--; break;          // Izquierda
        case 7: (*x)--; (*y)--; break; // Arriba-Izquierda
    }
}

bool validate_move(unsigned char dir, GameState* state, int my_id) {
    int my_x = state->players[my_id].x;
    int my_y = state->players[my_id].y;
    int* board = state->board;
    int width = state->width;
    int height = state->height;
    
    // Verificar si la dirección es válida
    int new_x = my_x;
    int new_y = my_y;

    // Calcular nueva posición según la dirección
    modify_x_y_acording_to_dir(dir, &new_x, &new_y);

    // Verificar límites del tablero
    if (new_x < 0 || new_x >= width || new_y < 0 || new_y >= height) {
        return false;
    }

    // Verificar si la celda está ocupada
    int index = new_y * width + new_x;
    if (board[index] <= 0) {
        return false;
    }

    return true;
}

// Chequea si todas las direcciones están bloqueadas
bool is_blocked(int player_id, GameState* state) {
    for (unsigned char dir = 0; dir < 8; dir++) {
        if (validate_move(dir, state, player_id)) {
            return false;
        }
    }
    return true;
}


// Mueve al jugador a la nueva posición y actualiza el puntaje
// y el tablero
void move_player(int player_id, unsigned char dir, GameState* state) {
    int* board = state->board;
    int width = state->width;

    // Obtener la posición actual del jugador
    int my_x = state->players[player_id].x;
    int my_y = state->players[player_id].y;

    // Calcular nueva posición según la dirección
    modify_x_y_acording_to_dir(dir, &my_x, &my_y);

    // Actualizar la posición del jugador
    state->players[player_id].x = my_x;
    state->players[player_id].y = my_y;
    state->players[player_id].score += board[my_y * width + my_x]; // Sumar el valor de la celda al puntaje

    // Actualizar el tablero
    board[my_y * width + my_x] = -player_id; // Marcar la celda como ocupada por el jugador
}


// Intenta mover al jugador en la dirección especificada
void try_to_move_player(int player_id, unsigned char dir, GameState* state) {
    if (validate_move(dir, state, player_id)) {
        move_player(player_id, dir, state);
        state->players[player_id].valid_moves++;
    } else {
        state->players[player_id].invalid_moves++;
    }
}

// Verifica si todos los jugadores están bloqueados
bool check_for_blocking(GameState* state) {
    bool all_blocked = true;
    for (int player_id = 0; player_id < state->player_count; player_id++) {
        if (!state->players[player_id].is_blocked) {
            bool blocked = is_blocked(player_id, state);
            state->players[player_id].is_blocked = blocked;
            if (!blocked) {
                all_blocked = false;
            }
        }
    }
    return all_blocked;
}

int main(int argc, char* argv[]) {
    // Validar argumentos
    validate_args(argc, argv);

    // Crear memoria compartida del estado (solo máster la puede escribir, los demás la leen)
    int shm_fd = shm_open(SHM_STATE, O_CREAT | O_RDWR, 0644);
    ftruncate(shm_fd, sizeof(GameState) + sizeof(int) * width * height);
    GameState* state = mmap(NULL, sizeof(GameState) + sizeof(int) * width * height, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    
    // Inicializar el estado del juego
    init_game_state(state);
    
    // Crear memoria compartida de sincronización
    int shm_sync_fd = shm_open(SHM_SYNC, O_CREAT | O_RDWR, 0666);
    fchmod(shm_sync_fd, 0666);
    ftruncate(shm_sync_fd, sizeof(SyncState));
    SyncState* sync = mmap(NULL, sizeof(SyncState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_sync_fd, 0);

    // Inicializar semáforos
    init_sync_state(sync);


    printf("Máster listo. Memoria y semáforos inicializados.\n");

    // ... Aquí iría la lógica de creación de jugadores, control del juego, etc.

    create_players(state);
    create_view();

    // Permite a la vista imprimir el estado inicial, espero a que termine para continuar
    sem_post(&sync->changes_available);
    sem_post(&sync->game_state_mutex);

    while (!state->is_finished) {
        // fd_set readfds;
        // FD_ZERO(&readfds);
        // int maxfd = -1;
        // bool hay_activos = false;

        // for (int i = 0; i < player_count; i++) {
        //     if (!procesos[i].bloqueado) {
        //         FD_SET(procesos[i].pipe_read_fd, &readfds);
        //         if (procesos[i].pipe_read_fd > maxfd)
        //             maxfd = procesos[i].pipe_read_fd;
        //         hay_activos = true;
        //     }
        // }


        // VER EN QUÉ DIRECCIÓN MOVERSE, PROBABLEMENTE CON UN SELECT CON LOS PIPES
        unsigned char dir;
        int player_id;


        // Para modificar el estado del juego, el máster debe tener el mutex (avisa con el de starvation que quiere entrar)
        sem_wait(&sync->print_done);
        sem_wait(&sync->starvation_mutex);
        sem_wait(&sync->game_state_mutex);
        sem_post(&sync->starvation_mutex);

        
        // SABIENDO LA DIRECCIÓN, INTENTAR MOVERSE
        // INCREMENTAR SCORE/VALID MOVES/INVALID MOVES
        // EN LA NUEVA POSICIÓN, VER SI ESTÁ BLOQUEADO
        // EVALUAR SI HAY CONDICIÓN DE BREAK (TERMINAR JUEGO)
        try_to_move_player(player_id, dir, state);
        state->is_finished = check_for_blocking(state);
        

        sem_post(&sync->changes_available);
        sem_post(&sync->game_state_mutex);

        usleep(delay * 1000);


        // if (!hay_activos) {
        //     printf("Todos los jugadores están bloqueados.\n");
        //     state->is_finished = true;
        //     break;
        // }

        // struct timeval tv = { .tv_sec = timeout, .tv_usec = 0 };
        // int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);

        // if (ready < 0) {
        //     perror("select");
        //     break;
        // } else if (ready == 0) {
        //     printf("Timeout: ningún movimiento válido.\n");
        //     state->is_finished = true;
        //     break;
        // }

        // for (int i = 0; i < player_count; i++) {
        //     int fd = procesos[i].pipe_read_fd;
        //     if (FD_ISSET(fd, &readfds)) {
        //         unsigned char mov;
        //         int n = read(fd, &mov, 1);
        //         if (n == 1) {
        //             // TODO: validar y procesar el movimiento
        //             // validar_movimiento(i, mov, state);
        //             state->players[i].valid_moves++; // por ejemplo
        //             printf("[Jugador %d] movimiento recibido: %d\n", i, mov);

        //             // avisar a la vista
        //             sem_post(&sync->changes_available);
        //             sem_wait(&sync->print_done);
        //         } else {
        //             // EOF
        //             procesos[i].bloqueado = true;
        //             close(fd);
        //             printf("[Jugador %d] EOF, se bloqueó\n", i);
        //         }
        //     }
        // }
    }

    for (int i = 0; i < player_count; i++) {
        waitpid(procesos[i].pid, NULL, 0); // Esperar a que el jugador termine
    }

    // Limpiar memoria compartida
    if (munmap(state, sizeof(GameState) + sizeof(int) * width * height) == -1) {
        perror("munmap state");
    }
    if (munmap(sync, sizeof(SyncState)) == -1) {
        perror("munmap sync");
    }
    if (close(shm_fd) == -1) {
        perror("close shm_fd");
    }
    if (close(shm_sync_fd) == -1) {
        perror("close shm_sync_fd");
    }
    if (shm_unlink(SHM_STATE) == -1) {
        perror("shm_unlink state");
    }
    if (shm_unlink(SHM_SYNC) == -1) {
        perror("shm_unlink sync");
    }
    printf("Máster terminado.\n");
    return 0;
}

