#include "utils.h"
#include "xerrori.h"

struct sockaddr_in servaddr;

int parse_args (int argc, char *argv[], options_t *options) {

    options->nthread = 4;
    options->qlen = 8;
    options->delay = 0;

    int opt, arg;
    while ((opt = getopt(argc, argv, "n:q:d:")) != -1) {
        switch (opt) {
            case 'n':
                if (myIsNumber(optarg) == 1) {
                    arg = atoi(optarg);
                    if (arg > 0)
                        options->nthread = arg;
                    break;
                }
                fprintf(stderr, "Invalid argument provided for -n, using default value: %d\n", 4);
                break;
            case 'q':
                if (myIsNumber(optarg) == 1) {
                    arg = atoi(optarg);
                    if (arg > 0)
                        options->qlen = arg;
                    break;
                }
                fprintf(stderr, "Invalid argument provided for -n, using default value: %d\n", 4);
                break;
            case 'd':
                if (myIsNumber(optarg) == 1) {
                    arg = atoi(optarg);
                    if (arg >= 0)
                        options->delay = arg;
                    break;
                }
                fprintf(stderr, "Invalid argument provided for -n, using default value: %d\n", 4);
                break;
            case '?':
                if (optopt == 'n' || optopt == 'q' || optopt == 'd') {
                    fprintf(stderr, "Option -%c requires an argument. Using default value...\n", optopt);
                    break;
                }
                fprintf(stderr, "Unknown option: -%c | Usage: %s [-n nthread] [-q qlen] [-d delay] file1 file2 ...\n", optopt, argv[0]);
                return -1;
            default:
                fprintf(stderr, "Usage: %s [-n nthread] [-q qlen] [-d delay] file1 file2 ...\n", argv[0]);
                return -1;
        }
    }
    return 0;
}

int producer(buffer_t *buf, char *argvi) {
    xpthread_mutex_lock(buf->buf_lock, QUI);
    while (buf->count == buf->buf_len) {
        puts("Buffer pieno, attendo...");
        xpthread_cond_wait(buf->not_full, buf->buf_lock, QUI);
    }
    assert(buf->count < buf->buf_len);
    buf->buffer[buf->tail] = argvi;
    buf->tail++;
    buf->tail %= buf->buf_len;
    buf->count++;
    xpthread_cond_signal(buf->not_empty, QUI);
    xpthread_mutex_unlock(buf->buf_lock, QUI);
    return 0;
}

char* consumer(buffer_t *buf) {
    while (buf->count == 0) {
        puts("Buffer vuoto, attendo...");
        xpthread_cond_wait(buf->not_empty, buf->buf_lock, QUI);
    }
    assert(buf->count > 0);
    strcpy(buf->filename, buf->buffer[buf->head]);
    buf->head++;
    buf->head %= buf->buf_len;
    buf->count--;
    
    if (strcmp(buf->filename, POISON_PILL) == 0) {
        //printf("[Worker%ld] Ricevuto poison pill, termino thread...\n", pthread_self());
        return NULL;
    }
    // xpthread_mutex_unlock(buf->buf_lock, QUI);
    // unlock in workerTask function
    return buf->filename;
}


char* getSomma(char *fname) {
    //printf("Thread%ld, file: %s\n", pthread_self(), fname);
    FILE *fp = xfopen(fname, "r", QUI);
    long n;
    long somma = 0;
    int i = 0;
    while (fread(&n, sizeof(long), 1, fp) > 0) {
        somma += (i*n);
        i++;
    }
    fclose(fp);
    // https://stackoverflow.com/questions/1383649/concatenating-strings-in-c-which-method-is-more-efficient
    if (asprintf(&fname, "%s %ld", fname, somma) < 0)
        xtermina("Errore asprintf", QUI);
    return fname; // formato <file: somma>
}


void *workerTask(void *args) {
    buffer_t *buf = (buffer_t *) args;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // From man page: -1 is returned if an error occurs, otherwise the return value is a descriptor referencing the socket.
	if (sockfd == -1)
	    xtermina("Errore creazione socket", QUI);
    //puts("Socket creata con successo");

	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(IP);
	servaddr.sin_port = htons(PORT);

	if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) == -1)
        xtermina("Connessione fallita", QUI);
    //puts("Connessione al sever riuscita");

    while (1) {
    xpthread_mutex_lock(buf->buf_lock, QUI);
    char *f = (char *) malloc(MAX_FILENAME_LEN);
    if ((f=consumer(buf)) == NULL) {
        // end of communication
        free(f);
        xpthread_cond_signal(buf->not_full, QUI);
        xpthread_mutex_unlock(buf->buf_lock, QUI);
        int w, tmp;
        tmp = strlen(END_OF_COMMUNICATION);
        w = writen(sockfd, &tmp, sizeof(int));
        if (w != sizeof(int))
            xtermina("Errore scrittura su socket", QUI);
        //printf("Avvisato il socket che il messaggio sarà lungo %lu bytes\n", strlen(msg));

        w = writen(sockfd, END_OF_COMMUNICATION, tmp);
        if (w != tmp)
            xtermina("Errore scrittura su socket", QUI);
        break;
    }

    char* sommaFile = getSomma(f);
    xpthread_cond_signal(buf->not_full, QUI);
    xpthread_mutex_unlock(buf->buf_lock, QUI);

    int w, tmp;
    char dummy[] = "Worker ";
    char *msg = (char *) malloc(strlen(dummy) + strlen(sommaFile) + 1);
    strcpy(msg, dummy);
    strcat(msg, sommaFile);
    free(sommaFile);

    tmp = strlen(msg);
    w = writen(sockfd, &tmp, sizeof(int));
    if (w != sizeof(int))
      xtermina("Errore scrittura su socket", QUI);
    //printf("Avvisato il socket che il messaggio sarà lungo %lu bytes\n", strlen(msg));

    w = writen(sockfd, msg, tmp);
    if (w != tmp) {
      xtermina("Errore scrittura su socket", QUI);
    }
    printf("Sent message to server: %s\n", msg);
    free(msg);
    }
    close(sockfd);
    return NULL;
}

// disclosure: http://didawiki.cli.di.unipi.it/doku.php/informatica/sol/laboratorio20/esercitazionib/readnwriten
/* Read "n" bytes from a descriptor */
ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}
 
/* Write "n" bytes to a descriptor */
ssize_t writen(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nwritten;
 
   nleft = n;
   while (nleft > 0) {
     if((nwritten = write(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount written so far */
     } else if (nwritten == 0) break; 
     nleft -= nwritten;
     ptr   += nwritten;
   }
   return(n - nleft); /* return >= 0 */
}

// isNumber() non detecta caratteri/anomalie all'interno della stringa perché testa solo il primo carattere
// per ovviare a questo problema ho definito la funzione myIsNumber() che testa tutti i caratteri della stringa tranne il terminatore
int myIsNumber(char* str) {
    int i = 0;
    if (str[0] == '-')
        i = 1;
    while (i < strlen(str)) {
        if (!isdigit(str[i]))
            return 0;
        i++;
    }
    return 1;
}


void *handleSIG(void *args) {
    signal_t *sigstruct = (signal_t *) args;
    while (1) {
        int sig;
        sigwait(sigstruct->mask, &sig);
        if (sig == SIGINT) {
            sigstruct->gotSIG = 1;
            puts(" Ricevuto segnale SIGINT, termino...");
            break;
        }
    }
}