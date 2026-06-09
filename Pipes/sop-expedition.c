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
    int pipe[2];

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



void child_work(int k, int M, int pipes[][2]) {
    int x = k/M;
    int y = k % M;

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
    exit(EXIT_SUCCESS);
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

int main(int argc, char* argv[])
{
    if (argc != 2) {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }


    int M = atoi(argv[1]);

    if (M <= 2 || M >= 6) {
        return EXIT_FAILURE;
    }

    int num_nodes = M*M;

    int pipes[num_nodes][2];


    for (int i =0;i<num_nodes;i++) {
        if (pipe(pipes[i]) < 0) {
            ERR("pipe");
        }
    }


    for (int k = 0; k < num_nodes; k++) {
        pid_t pid = fork();

        if (pid == 0) {
            child_work(k, M, pipes);
        }
    }
    for (int i=0; i < num_nodes;i++) {
        close(pipes[i][0]);
    }
    printf("Mothership: Opened %d descriptors\n",count_descriptors());


    for (int i =0;i<num_nodes;i++) {
        wait(NULL);
    }

    for (int i =0;i<num_nodes;i++) {
        close(pipes[i][1]);
    }
    printf("Mothership: Opened %d descriptors\n",count_descriptors());
    return 0;
}