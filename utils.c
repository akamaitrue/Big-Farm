/*
* @author: Francesco Massellucci
* @brief: Progetto Big-Farm per il corso di Laboratorio II A.A. 2021/2022
* @file: utils.c
* @date: 14/12/2022
*/

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
        //puts("Buffer pieno, attendo...");
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

// lock e unlock per prelevare dal buffer sono gestite all'interno della funzione workerTask
char* consumer(buffer_t *buf) {
    while (buf->count == 0) {
        //puts("Buffer vuoto, attendo...");
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
    return buf->filename;
}


char* getSomma(char *fname) {
    //printf("Thread%ld, file: %s\n", pthread_self(), fname);
    FILE *fp = xfopen(fname, "r", QUI);
    long long n;
    long long somma = 0; // long long per evitare overflow, sizeof(long long) = 8 bytes per interi di 64 bit

    int i = 0;
    while (fread(&n, sizeof(long), 1, fp) > 0) {
        somma += (i*n);
        i++;
    }
    fclose(fp);

    // restituisco una stringa nel formato <file: somma>
    if (asprintf(&fname, "%s %lld", fname, somma) < 0)
        xtermina("Errore allocazione memoria (asprintf)", QUI);
    return fname;
}


void sendMsg(int fd, char *msg) {
    int w, itmp;

    itmp = strlen(msg);
    w = writen(fd, &itmp, sizeof(itmp));
    if (w != sizeof(int))
        xtermina("Errore scrittura su socket", QUI);
    //printf("Avvisato il socket che il messaggio sarà lungo %d bytes\n", itmp);

    // invio il messaggio vero e proprio
    w = writen(fd, msg, itmp);
    if (w != itmp)
        xtermina("Errore scrittura su socket", QUI);
}

char* getResponse(int fd) {

    int r, itmp, resSize;
    char *ires;

    r = readn(fd, &itmp, sizeof(itmp));    
    if (r != sizeof(int))
        xtermina("Errore lettura da socket", QUI);

    resSize = ntohl(itmp);
    ires = (char *) malloc(resSize + 1);
    ires[resSize] = '\0';
    r = readn(fd, ires, resSize);
    if (r != resSize)
        xtermina("Errore lettura da socket", QUI);

    return ires;
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
    f = consumer(buf);
    if (f == NULL) {
        // end of communication
        free(f);
        xpthread_cond_signal(buf->not_full, QUI);
        xpthread_mutex_unlock(buf->buf_lock, QUI);
        
        sendMsg(sockfd, END_OF_COMMUNICATION);
        close(sockfd);
        break;
    }

    char* sommaFile = getSomma(f);
    xpthread_cond_signal(buf->not_full, QUI);
    xpthread_mutex_unlock(buf->buf_lock, QUI);

    char dummy[] = "Worker ";
    char *msg = (char *) malloc(strlen(dummy) + strlen(sommaFile) + 1);
    strcpy(msg, dummy);
    strcat(msg, sommaFile);
    free(sommaFile);

    sendMsg(sockfd, msg);
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
    // la funzione accetta anche numeri negativi
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
    // il thread handler che esegue questa routine resta in attesa di un segnale
    while (1) {
        int sig;
        sigwait(sigstruct->mask, &sig);
        // la terminazione pulita è solo per il segnale SIGINT
        if (sig == SIGINT) {
            sigstruct->gotSIG = 1;
            puts(" Ricevuto segnale SIGINT, termino...");
            break;
        }
    }
    return NULL;
}
