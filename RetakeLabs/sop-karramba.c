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
    int x = rand() % (n+1);
    int y = rand() % (m+1);

    for (int i =0;i<STEP_COUNT;i++) {

        char move = get_random_move(board, x,y, n, m);

        set_char(board,x,y,n,m,'=');

        move_pos(board,move, n, m, &x,&y);

        set_char(board, x, y, n, m, 'S');

        ms_sleep(100);

    }

    printf("Smok-Expedition completed!\n");

    munmap(board, file_size);

    exit(EXIT_SUCCESS);

}