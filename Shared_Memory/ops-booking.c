#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>

#define MIN_N 1
#define MAX_N 15
#define MIN_M 5
#define MAX_M 20

#define SEMAPHORE_NAME "/gate"
#define SHM_NAME "/houses"

#define ERR(source)                                     \
do                                                  \
{                                                   \
fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
perror(source);                                 \
kill(0, SIGKILL);                               \
exit(EXIT_FAILURE);                             \
} while (0)


//W PAMIECI DZIELONEJ NIE MOZE BYC POINTEROW
typedef struct {
    int counter;
    pthread_cond_t cond;
    pthread_mutex_t shared_mutexes[MAX_N];
    pthread_mutex_t counter_mutex;
    pthread_mutex_t shm_mutex;
}shared_data_t;

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of townhouses, %d <= n <= %d\n", MIN_N, MAX_N);
    fprintf(stderr, "\t  m - number of nobles, %d <= m <= %d\n", MIN_M, MAX_M);
    exit(EXIT_FAILURE);
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void child_work(int n, shared_data_t* shared_data) {
    srand(getpid());
    int is_working = 1;
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    int* shm_data = mmap(NULL, n*sizeof(int),PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    //otwieramy semafor nazwany w kazdym dziecku
    sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 10);
    sem_wait(semaphore);
    while (is_working) {
        int is_available = 0;
        for (int i =0;i< n;i++) {
            if (pthread_mutex_trylock(&shared_data->shared_mutexes[i])==0) {
                pthread_mutex_lock(&shared_data->counter_mutex);
                shared_data->counter--;
                pthread_mutex_unlock(&shared_data->counter_mutex);

                is_available = 1;
                printf("[%d] Such a comfortable house n. <%d>!\n", getpid(), i);
                ms_sleep(300);

                pthread_mutex_lock(&shared_data->counter_mutex);

                shared_data->counter++;

                pthread_mutex_unlock(&shared_data->counter_mutex);

                pthread_cond_signal(&shared_data->cond);

                if (rand() % 100 < 10) {
                    pthread_mutex_lock(&shared_data->shm_mutex);
                    printf("[%d] Time to break a wall in house <%d>!\n", getpid(), i);
                    shm_data[i] = 1;
                    pthread_mutex_unlock(&shared_data->shm_mutex);
                }
                pthread_mutex_unlock(&shared_data->shared_mutexes[i]);
                is_working = 0;
                break;
            }
        }
        if (!is_available) {
            sem_post(semaphore);
            printf("[%d] Cui bono have I arrived here?\n", getpid());

            pthread_mutex_lock(&shared_data->counter_mutex);
            while (shared_data->counter == 0) {
                pthread_cond_wait(&shared_data->cond, &shared_data->counter_mutex);
            }
            pthread_mutex_unlock(&shared_data->counter_mutex);
            printf("[%d] Let's try again\n", getpid());
        }
    }

    sem_post(semaphore);
    sem_close(semaphore);

    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        usage(argv[0]);
    }

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);

    int counter = n;
    shared_data_t* shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_unlink(SEMAPHORE_NAME);

    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    //nadajemy rozmiar pamieci dzielonej!!!!
    ftruncate(shm_fd, n*sizeof(int));
    //mapujemy to na pamiec wspolna

    //WAZNE rm /dev/shm/ zeby usunac shared mem jesli zapomnimy zunlinkowac
    int* shm_data = mmap(NULL, n*sizeof(int),PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    //deskryptor nie jest juz potrzebny
    close(shm_fd);

    for (int i =0;i<n;i++) {
        shm_data[i] = 0;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_data->counter_mutex, &attr);
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shared_data->cond, &condattr);
    pthread_mutex_init(&shared_data->shm_mutex, &attr);
    for (int i =0; i< n; i++) {
        pthread_mutex_init(&shared_data->shared_mutexes[i], &attr);
    }
    shared_data->counter = counter;
    pthread_condattr_destroy(&condattr);
    pthread_mutexattr_destroy(&attr);

    for (int i =0; i<m;i++) {
        pid_t pid = fork();

        if (pid == 0) {
            child_work(n, shared_data);
        }
    }
    //waitpid() - waiting for specific child, wait(NULL) waiting for any child z WNOHANG to nieblokujacy
    while (wait(NULL) > 0){}
    for (int i =0;i<n;i++) {
        printf("%d ", shm_data[i]);
    }
    //zwalnia pamiec z mmap(tylko ten proces odcina sie od pamieci)
    munmap(shared_data, sizeof(shared_data_t));
    munmap(shm_data, n*sizeof(int));
    //usuwa plik shm
    shm_unlink(SHM_NAME);

    sem_unlink(SEMAPHORE_NAME);
    exit(EXIT_SUCCESS);
}