#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define MOTHERSHIP_FIFO "/tmp/mothership"
#define SUPPLIES_CAPACITY 4

typedef struct
{
    int id;        // 100, 99, ..., 1, 0
    int supplies;  // Current supplies
} expedition_t;

void usage(int argc, char* argv[])
{
    fprintf(stderr, "Usage: %s M\n", argv[0]);
    fprintf(stderr, "  M - size of the board (3 <= M <= 6)\n");
    exit(EXIT_FAILURE);
}

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}


void sigchld_handler(int sig)
{
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid)
            return;
        if (0 >= pid)
        {
            if (ECHILD == errno)
                return;
            ERR("waitpid:");
        }
    }
}
void msleep(const int ms)
{
    struct timespec tt;
    tt.tv_sec = ms / 1000;
    tt.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}



void child_work(int k, int M, int pipes[][2]) {
    srand(k);
    int x = k/M;
    int y = k % M;

    int local_supplies = rand() % 3;

    int n_idx = ((x - 1 + M) % M) * M + y;
    int s_idx = ((x + 1) % M) * M + y;
    int w_idx = x * M + ((y - 1 + M) % M);
    int e_idx = x * M + ((y + 1) % M);
    for (int i =0; i< M*M; i++) {
        if (i!=k) {
            close(pipes[i][0]);
        }
        if (i!=n_idx && i!=s_idx && i!=w_idx && i!=e_idx) {
            close(pipes[i][1]);
        }
    }

    printf("Node [%d] (%d,%d): Opened %d descriptors\n",k,x,y,count_descriptors());

    int neighbors[4] = { n_idx, s_idx, w_idx, e_idx };

    expedition_t exp;

    while (1) {
        if (read(pipes[k][0], &exp, sizeof(exp))>0) {
            int collapse_number = 1 + rand() % 100;

            if (collapse_number % 20 == 0) {
                printf("Node %d (%d,%d): Collapsing\n", k,x,y);
                close(pipes[k][0]);

                close(pipes[n_idx][1]);

                close(pipes[s_idx][1]);

                close(pipes[w_idx][1]);

                close(pipes[e_idx][1]);

                exit(EXIT_SUCCESS);

            }
            if (exp.id == 0) {

                close(pipes[k][0]);

                close(pipes[n_idx][1]);

                close(pipes[s_idx][1]);

                close(pipes[w_idx][1]);

                close(pipes[e_idx][1]);

                printf("Node [%d] (%d,%d): Opened %d descriptors\n",k,x,y,count_descriptors());

                exit(EXIT_SUCCESS);
            }

            if (local_supplies >0) {
                int needed = SUPPLIES_CAPACITY - exp.supplies;
                if (needed > 0) {
                    int taken = (local_supplies < needed) ? local_supplies : needed;
                    exp.supplies += taken;
                    local_supplies -= taken;
                    printf("Expedition %d: Good stuff.\n", exp.id);
                }
            }

            int should_check_collapse = 0;
            if (exp.supplies > 0) {
                printf("Expedition %d: Tomorrow comes",exp.id);
                exp.supplies--;
                msleep(200);

                int start_neighbor_idx = rand() % 4;

                int forwarded = 0;

                for (int i =0;i<4;i++) {
                    int current_neighbor = neighbors[(start_neighbor_idx + i) % 4];

                    if (write(pipes[current_neighbor][1], &exp, sizeof(exp)) >= 0) {
                        forwarded = 1;
                        should_check_collapse = 1;
                        break;
                    }
                }

                if (!forwarded) {
                    printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                    close(pipes[k][0]);
                    close(pipes[n_idx][1]); close(pipes[s_idx][1]);
                    close(pipes[w_idx][1]); close(pipes[e_idx][1]);
                    exit(EXIT_SUCCESS);
                }

            }
            else {
                printf("Expedition %d: For those who come after\n",exp.id);
                local_supplies+=2;
            }

            if (should_check_collapse) {
                if ((rand() % 100) < 5) {
                    printf("Node [%d] (%d,%d): Collapsing\n", k, x, y);
                    close(pipes[k][0]);
                    close(pipes[n_idx][1]); close(pipes[s_idx][1]);
                    close(pipes[w_idx][1]); close(pipes[e_idx][1]);
                    exit(EXIT_SUCCESS);
                }
            }
            printf("Expedition %d: arrived at node %d (%d,%d)\n",exp.id, k, x, y);
        }

    }
}




void mothership_work(int fd, int M, int pipes[][2]) {
    char line_buffer[64];
    ssize_t bytes_read;
    char c;
    int index = 0;
    int exp_id = 100;
    while ((bytes_read = read(fd,&c,1)) >0) {
        line_buffer[index] = c;

        index++;

        if (c=='\n') {
            line_buffer[index] = '\0';

            char cmd[32];
            int x,y;

            int parsed_fields = sscanf(line_buffer, "%31s %d %d \n",cmd, &x,&y);

            if (parsed_fields == 3 && strcmp("SPAWN", cmd) == 0) {
                expedition_t exp;
                exp.id = exp_id;
                exp.supplies = SUPPLIES_CAPACITY;
                exp_id--;

                int k = x*M + y;

                if (write(pipes[k][1], &exp, sizeof(exp)) < 0) {
                    //TO ZNACZY ZE DESKRYPTOR JEST ZAMKNIETY
                    if (errno == EPIPE) {
                        printf("Expedition %d: failed\n", exp.id);
                    }
                }

            }
        }
    }

    expedition_t exp;
    exp.id = 0;
    exp.supplies = SUPPLIES_CAPACITY;

    for (int i =0; i<M*M;i++) {
        write(pipes[i][1], &exp, sizeof(exp));
    }
}
int main(int argc, char* argv[])
{
    if (argc != 2) {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    //WAŻNE!!!! JEŚLI IGNORUJEMY SIGPIPE I PROBUJEMY WRITOWAC DO ZAMKNIETEJ RURY TO ZWROCI NAM -1
    signal(SIGPIPE, SIG_IGN);

    int M = atoi(argv[1]);

    if (M <= 2 || M >= 6) {
        return EXIT_FAILURE;
    }

    int num_nodes = M*M;

    int pipes[num_nodes][2];
    if (mkfifo(MOTHERSHIP_FIFO, 0666) < 0) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }

    for (int i =0;i<num_nodes;i++) {
        if (pipe(pipes[i]) < 0) {
            ERR("pipe");
        }
    }

    //zamykamy wszystkie read ends dla mothership
    for (int i=0; i < num_nodes;i++) {
        close(pipes[i][0]);
    }

    for (int k = 0; k < num_nodes; k++) {
        pid_t pid = fork();

        if (pid == 0) {
            child_work(k, M, pipes);
        }
    }

    //WAŻNE!!! Otwieramy fifo po tworzeniu pipow poniewaz open w trybie o_rdonly zablokuje mothership
    // jesli jeszcze nikt nie pisze czyli fifo jest otwarte tylko do zapisu to system usypia proces na tej linijce, dopiero kiedy wpiszemy echo "SPAWN 0 0" > /tmp/mothership system ma komplet read i write i leci dalej

    int fifo_fd = open(MOTHERSHIP_FIFO, O_RDONLY);

    mothership_work(fifo_fd, M, pipes);

    printf("Mothership: Opened %d descriptors\n",count_descriptors());

    //czekamy az wszystkie dzieci skoncza
    for (int i =0;i<num_nodes;i++) {
        wait(NULL);
    }

    for (int i =0;i<num_nodes;i++) {
        close(pipes[i][1]);
    }
    printf("Mothership: Opened %d descriptors\n",count_descriptors());



    unlink(MOTHERSHIP_FIFO);
    return 0;
}