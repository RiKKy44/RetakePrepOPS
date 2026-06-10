#include "common.h"
#include "board_utils.h"

#define BOARD_FILE "board"
#define FIFO_NAME "fifo"
#define STEP_COUNT 500
#define WAIT_N 10

#define PORT 12345

#define EPOLL_MAX_EVENTS 10

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");

    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n, m - board width and height, respectively\n");

    exit(EXIT_FAILURE);
}



typedef struct {
    pthread_mutex_t map_mutex;
}shared_data_t;



void child_work(char* board,shared_data_t* process_data, int n, int m,int startx, int starty) {
    int x = startx;
    int y = starty;
    ms_sleep(WAIT_N *100);
    for (int i =0;i<STEP_COUNT;i++) {
        pthread_mutex_lock(&process_data->map_mutex);
        if (has_trail(board, x, y, n, m)!=0) {
            set_char(board, x, y, n, m, ' ');
        }
        else {
            set_char(board, x, y, n, m, '.');
            printf("Carramba!\n");
        }
        char move = get_trail_move(board, x, y, n, m);

        move_pos(board, move, n,m, &x,&y);
        pthread_mutex_unlock(&process_data->map_mutex);
        ms_sleep(100);
    }
    exit(EXIT_SUCCESS);
}
int main(int argc, char** argv) {
    if (argc!=3) {
        usage(argv[0]);
    }
    int n = atoi(argv[1]);

    int m = atoi(argv[2]);

    int fd = open(BOARD_FILE, O_CREAT | O_RDWR | O_TRUNC, 0666);

    ssize_t file_size = m*(n+1);

    ftruncate(fd, file_size);

    char* board = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd , 0);

    close(fd);

    fill_board(board, n, m);
    int x = rand() % (n);
    int y = rand() % (m);

    shared_data_t* shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_data->map_mutex, &attr);
    pid_t pid = fork();

    if (pid ==0) {
        child_work(board,shared_data,n,m,x,y);
    }
    for (int i =0;i<STEP_COUNT;i++) {
        pthread_mutex_lock(&shared_data->map_mutex);
        char move = get_random_move(board, x,y, n, m);

        set_char(board,x,y,n,m,'=');

        move_pos(board,move, n, m, &x,&y);

        set_char(board, x, y, n, m, 'S');

        pthread_mutex_unlock(&shared_data->map_mutex);
        ms_sleep(100);
    }

    printf("Smok-Expedition completed!\n");
    wait(NULL);
    munmap(board, file_size);
    munmap(shared_data, sizeof(shared_data_t));
    exit(EXIT_SUCCESS);

}