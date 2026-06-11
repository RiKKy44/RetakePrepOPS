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
    int fifo_fd = open(FIFO_NAME, O_RDONLY);
    ms_sleep(WAIT_N *100);

    int socket_fd = bind_tcp_socket(PORT, EPOLL_MAX_EVENTS);
    //Tworzymy epoll zeby nasluchiwac na deskryptorze socketu ORAZ fifo
    int epoll_fd = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fifo_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fifo_fd, &event);

    event.events = EPOLLIN;
    event.data.fd = socket_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event);
    struct epoll_event events[EPOLL_MAX_EVENTS];
    char move;
    int keep_running = 1;
    int active_client_fd = -1;
    while (keep_running) {
        //read zwraca 0 kiedy proces piszacy zamknal polaczenie i nie wplyna nowe dane

        //epoll wait zasypia budzi sie jesli cos sie stanie na fifo lub sockecie
        int nfds = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);

        for (int i =0;i<nfds;i++) {
            if (events[i].data.fd == fifo_fd) {
                ssize_t received_bytes = read(fifo_fd, &move, 1);
                if (received_bytes > 0) {
                    printf("Direction %c? Don't try these tricks on me, carramba!\n", move);

                    pthread_mutex_lock(&process_data->map_mutex);

                    if (has_trail(board, x, y, n, m) != 0) {
                        set_char(board, x, y, n, m, ' ');
                    } else {
                        set_char(board, x, y, n, m, '.');
                        printf("Carramba!\n");
                    }

                    char trail_move = get_trail_move(board, x, y, n, m);
                    move_pos(board, trail_move, n, m, &x, &y);

                    pthread_mutex_unlock(&process_data->map_mutex);

                    ms_sleep(100);
                }
                if (received_bytes==0) {
                    keep_running = 0;
                }
            }
            else if (events[i].data.fd == socket_fd) {
                //musimy wywolac accept zeby zaakceptowac polaczenie od klienta
                active_client_fd = add_new_client(socket_fd);
                if (active_client_fd == -1) {
                    printf("Headquarters connected -- over!\n");
                    //dodajemy nowego clienta do epolla

                    struct epoll_event client_event;
                    client_event.events = EPOLLIN;
                    client_event.data.fd = active_client_fd;

                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, active_client_fd, &client_event);
                }
                else {
                    close(active_client_fd);
                }
            }
            else if (events[i].data.fd == active_client_fd) {
                char network_char;
                ssize_t bytes_received = read(active_client_fd, &network_char, 1);

                if (bytes_received>0) {
                    if (network_char == 'W' || network_char == 'A' || network_char == 'S' || network_char == 'D') {
                        printf("Message %c -- accepted!\n", network_char);

                        pthread_mutex_lock(&process_data->map_mutex);
                        set_char(board, x, y, n, m, '*');
                        move_pos(board, network_char, n, m, &x, &y);

                        pthread_mutex_unlock(&process_data->map_mutex);
                    }
                }
                else if (bytes_received == 0) {

                    //jesli klient sie rozlaczyl to usuwamy go z monitorowania epolla
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_client_fd, NULL);

                    close(active_client_fd);

                    active_client_fd = -1;
                }
            }
        }
    }
    if (active_client_fd != -1) {
        close(active_client_fd);
    }
    close(socket_fd);
    close(epoll_fd);
    close(fifo_fd);
    exit(EXIT_SUCCESS);
}
int main(int argc, char** argv) {
    if (argc!=3) {
        usage(argv[0]);
    }
    int n = atoi(argv[1]);

    int m = atoi(argv[2]);
    mkfifo(FIFO_NAME, 0666);

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
    pthread_mutexattr_destroy(&attr);
    pid_t pid = fork();

    if (pid ==0) {
        child_work(board,shared_data,n,m,x,y);
    }
    int fifo_fd = open(FIFO_NAME, O_WRONLY);

    set_handler(SIG_IGN, SIGPIPE);

    for (int i =0;i<STEP_COUNT;i++) {
        pthread_mutex_lock(&shared_data->map_mutex);
        char move = get_random_move(board, x,y, n, m);

        set_char(board,x,y,n,m,'=');

        move_pos(board,move, n, m, &x,&y);

        set_char(board, x, y, n, m, 'S');

        pthread_mutex_unlock(&shared_data->map_mutex);
        ms_sleep(100);

        char random_move = get_random_move(board, x, y, n, m);

        write(fifo_fd,&random_move,1);
    }
    close(fifo_fd);

    printf("Smok-Expedition completed!\n");

    wait(NULL);

    munmap(board, file_size);

    munmap(shared_data, sizeof(shared_data_t));
    unlink(FIFO_NAME);
    exit(EXIT_SUCCESS);


}