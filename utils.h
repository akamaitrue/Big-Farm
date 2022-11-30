#include "xerrori.h"
#define MAX_FILENAME_LEN 255
#define POISON_PILL "POISON_PILL"
#define END_OF_COMMUNICATION "__BYE__"
#define QUI __LINE__,__FILE__
#define IP "127.0.0.1"
#define PORT 57849
#define SA struct sockaddr


typedef struct {
    int nthread, qlen, delay;
} options_t;

typedef struct {
    char **buffer;
    char filename[MAX_FILENAME_LEN];
    int buf_len, head, tail, count;
    pthread_mutex_t *buf_lock;
    pthread_cond_t *not_full, *not_empty;
} buffer_t;

typedef struct {
    volatile sig_atomic_t gotSIG; // volatile in order to prevent variable getting cached
    sigset_t *mask;
} signal_t;


int parse_args(int argc, char *argv[], options_t *options);
int producer(buffer_t *buf, char *argvi);
char* consumer(buffer_t *buf);
void *workerTask(void *buf);
ssize_t readn(int fd, void *ptr, size_t n);
ssize_t writen(int fd, void *ptr, size_t n);
int myIsNumber(char *str);
void *handleSIG(void *s);