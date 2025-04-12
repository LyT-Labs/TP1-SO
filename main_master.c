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
#include <math.h> // Para usar sin() y cos()
#include <wait.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

// Esto no tiene tanto sentido creo yo, pero parece que el enunciado lo pide así
// Si está definido, un delay de 4 segundos se vuelve de 6 si la vista tarda 2 segundos en imprimir
#define DELAY_INCLUDES_VIEW


unsigned short width;
unsigned short height;
unsigned int delay;
unsigned int timeout;
unsigned int seed;
unsigned int player_count;
char* view = NULL;
int view_pid = -1;
char* player_paths[MAX_PLAYERS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

unsigned int player_count = 0;

int last_player_moved = 0;  // índice, no pid

typedef struct {
    pid_t pid;
    int pipe_read_fd;  // máster lee de acá
    bool active; // 1 si el jugador está activo, 0 si se cerró el pipe (ocurrió un EOF)
} PlayerProc;

PlayerProc processes[MAX_PLAYERS];


struct timeval last_msg_time;

void update_last_msg_time() {
    gettimeofday(&last_msg_time, NULL);
}

int get_remaining_timeout_sec(int total_timeout_sec) {
    struct timeval now;
    gettimeofday(&now, NULL);
    
    int elapsed = now.tv_sec - last_msg_time.tv_sec;
    int remaining = total_timeout_sec - elapsed;
    return remaining > 0 ? remaining : 0;
}


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
            if (player_count >= MAX_PLAYERS) {
                fprintf(stderr, "Número máximo de jugadores alcanzado: %d\n", MAX_PLAYERS);
                exit(EXIT_FAILURE);
            }
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

    // Inicializar el tablero
    srand(seed);
    for (int i = 0; i < width * height; i++) {
        state->board[i] = (rand() % 9) + 1; // Valores aleatorios entre 1 y 9
    }

    // Calcular el centro de la elipse
    int center_x = width / 2;
    int center_y = height / 2;

    // Calcular los semiejes de la elipse
    double semi_major_axis = width * 0.3;  // Eje mayor (30% del ancho del tablero)
    double semi_minor_axis = height * 0.3; // Eje menor (30% del alto del tablero)

    if (player_count >= 5) {
        semi_major_axis = width * 0.4;  // Eje mayor (40% del ancho del tablero)
        semi_minor_axis = height * 0.4; // Eje menor (40% del alto del tablero)
    }

    // Inicializar jugadores
    for (int i = 0; i < player_count; i++) {
        state->players[i].score = 0;
        state->players[i].invalid_moves = 0;
        state->players[i].valid_moves = 0;
        state->players[i].is_blocked = false;
        state->players[i].pid = -1;

        // Obtener el nombre del jugador
        char* last_slash = strrchr(player_paths[i], '/');
        if (last_slash != NULL) {
            strncpy(state->players[i].name, last_slash + 1, MAX_NAME - 1);
        } else {
            strncpy(state->players[i].name, player_paths[i], MAX_NAME - 1);
        }
        state->players[i].name[MAX_NAME - 1] = '\0'; // Asegurarse de que la cadena esté terminada

        // Calcular la posición del jugador en la elipse
        double angle = (2 * M_PI / player_count) * i; // Ángulo en radianes
        int x = center_x + (int)(semi_major_axis * cos(angle));
        int y = center_y + (int)(semi_minor_axis * sin(angle));

        // Asegurarse de que las posiciones estén dentro de los límites del tablero
        x = (x < 0) ? 0 : (x >= width ? width - 1 : x);
        y = (y < 0) ? 0 : (y >= height ? height - 1 : y);

        state->players[i].x = x;
        state->players[i].y = y;

        if (player_count == 1) {
            // Si solo hay un jugador, va al centro, como ChompChamps
            state->players[i].x = center_x;
            state->players[i].y = center_y;
        }

        // Marcar la celda como ocupada por el jugador
        state->board[state->players[i].y * width + state->players[i].x] = -i;
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

// Función para crear los procesos de los jugadores, no las inicializaciones (eso está en init_game_state)
// Se crean los pipes y se redirige la salida estándar de cada jugador al pipe correspondiente
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
            

            char *ancho_str, *alto_str;
            int ancho_len = snprintf(NULL, 0, "%hu", width) + 1;
            int alto_len = snprintf(NULL, 0, "%hu", height) + 1;

            ancho_str = malloc(ancho_len);
            alto_str = malloc(alto_len);

            if (ancho_str == NULL || alto_str == NULL) {
                perror("malloc");
                exit(1);
            }

            snprintf(ancho_str, ancho_len, "%hu", width);
            snprintf(alto_str, alto_len, "%hu", height);
            execl(player_paths[i], player_paths[i], ancho_str, alto_str, NULL);
            perror("execl jugador");
            exit(1);
        } else {
            // Proceso máster
            close(pipefd[1]); // Cierra escritura
            processes[i].pid = pid;
            processes[i].pipe_read_fd = pipefd[0];
            state->players[i].pid = pid;
            processes[i].active = true; // El jugador está activo
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
    } else {
        view_pid = pid;
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
    system("clear");

    // Crear memoria compartida del estado (solo máster la puede escribir, los demás la leen)
    int shm_fd = shm_open(SHM_STATE, O_CREAT | O_RDWR, 0644);
    if (shm_fd < 0) {
        perror("shm_open state");
        exit(EXIT_FAILURE);
    }
    if(ftruncate(shm_fd, sizeof(GameState) + sizeof(int) * width * height) == -1) {
        perror("ftruncate state");
        exit(EXIT_FAILURE);
    }

    
    GameState* state = mmap(NULL, sizeof(GameState) + sizeof(int) * width * height, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap state");
        exit(EXIT_FAILURE);
    }
    
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


    create_players(state);
    create_view();

    if(view) sem_post(&sync->changes_available);
    sem_post(&sync->game_state_mutex);

    #ifdef DELAY_INCLUDES_VIEW
        if(view) sem_wait(&sync->print_done);
    #endif

    if(view) usleep(delay * 1000); // Espera el delay antes de continuar

    while (!state->is_finished) {

        unsigned char dir;
        int player_id;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = -1;

        for (int i = 0; i < player_count; i++) {
            // agrega los pipes de los jugadores activos a la lista de lectura
            if (!state->players[i].is_blocked && processes[i].active) {
                FD_SET(processes[i].pipe_read_fd, &read_fds);
                if (processes[i].pipe_read_fd > max_fd)
                    max_fd = processes[i].pipe_read_fd;
            }
        }

        int remaining_timeout = get_remaining_timeout_sec(timeout);
        struct timeval tv = { .tv_sec = remaining_timeout, .tv_usec = 0 };
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        bool no_moves_found = false;

        if (ready < 0) {
            perror("select");
            break;
        } else if (ready == 0 || remaining_timeout == 0) {
            // Timeout, no hay movimientos disponibles
            printf("Timeout, no hay movimientos disponibles.\n");
            no_moves_found = true;
        }
        

        for (int offset = 1; offset <= player_count; offset++) {
            int index = (offset + last_player_moved) % player_count; // Ciclo circular
            
            
            int fd = processes[index].pipe_read_fd;
            if (FD_ISSET(fd, &read_fds)) {
                unsigned char mov;
                int n = read(fd, &mov, 1);
                
                if (n == 1) {
                    dir = mov;
                    player_id = index;
                    last_player_moved = index;
                    update_last_msg_time();
                    break;
                } else {
                    // EOF
                    close(fd);
                    processes[index].active = false;
                }
            }
        }


        // Si la vista actualiza "asincrónicamente" mientras leemos el pipe, solo la tengo que esperar al modificar el estado
        #ifndef DELAY_INCLUDES_VIEW
            if(view) sem_wait(&sync->print_done);
        #endif
        
        // Para modificar el estado del juego, el máster debe tener el mutex (avisa con el de starvation que quiere entrar)
        sem_wait(&sync->starvation_mutex);
        sem_wait(&sync->game_state_mutex);
        sem_post(&sync->starvation_mutex);
        
        if (no_moves_found) {
            // Si no hay movimientos pendientes, se termina el juego   
            state->is_finished = true;
            
        }else{
            // Movimiento del jugador y validación de condición de fin
            try_to_move_player(player_id, dir, state);
            state->is_finished = check_for_blocking(state);
        }
        

        if(view) sem_post(&sync->changes_available);
        sem_post(&sync->game_state_mutex);

        // Si quiero que la vista bloquee el master y que el delay se sume a lo que tarde la vista, tengo que esperar acá a que imprima
        #ifdef DELAY_INCLUDES_VIEW
            if(view) sem_wait(&sync->print_done);
        #endif

        if(view) usleep(delay * 1000);
    }

    // Espero a que la view termine
    if (view) {
        int status;
        view_pid = waitpid(view_pid, &status, 0);
        if (view_pid == -1) {
            perror("waitpid view");
        }
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            printf("View exited (%d)\n", exit_code);
        }
    }

    // Espero a que los hijos terminen e imprimo sus resultados
    for (int i = 0; i < player_count; i++) {
        int status;
        int pid = waitpid(processes[i].pid, &status, 0);
        if (pid == -1) {
            perror("waitpid");

        }
        if (WIFEXITED(status)){
            int exit_code = WEXITSTATUS(status);
            // Player player (0) exited (0) with a score of 0 / 0 / 0
            printf("Player %s (%d) exited (%d) with a score of %u / %u / %u\n", 
                   state->players[i].name, i, exit_code,
                   state->players[i].score, state->players[i].valid_moves,
                   state->players[i].invalid_moves);
            
        }
        if (processes[i].active) {
            close(processes[i].pipe_read_fd);
        }
        
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

