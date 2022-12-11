/*
* @author: Francesco Massellucci
* @brief: Progetto Big-Farm per il corso di Laboratorio II A.A. 2021/2022
* @file: farm.c
* @date: 11/12/2022
*/

#include "xerrori.h"
#include "utils.h"

int main(int argc, char *argv[]) {

  signal_t *sigs;
  sigs = (signal_t*) malloc(sizeof(signal_t));
  sigs->gotSIG = 0;
  sigs->mask = (sigset_t*) malloc(sizeof(sigset_t));
  sigfillset(sigs->mask); 
  // con questa maschera posso ignorare tutti i segnali tranne SIGINT, SIGKILL e SIGSTOP
  //ed eseguire l'handler per la terminazione pulita solo per SIGINT
  //sigemptyset(sigs->mask);
  sigaddset(sigs->mask, SIGINT);
  pthread_sigmask(SIG_BLOCK, sigs->mask, NULL);
  // la gestione del segnale SIGINT è delegata all'handler
  
  //faccio partire l'handler
  pthread_t handler;
  xpthread_create(&handler, NULL, handleSIG, sigs, QUI);

  options_t *options;
  buffer_t *buf_dati;
  options = (options_t *) malloc(sizeof(options_t));
  buf_dati = (buffer_t *) malloc(sizeof(buffer_t));

  // parsing command line arguments
  parse_args(argc, argv, options);
  printf("Using: NTHREAD=%d | QLEN=%d | DELAY=%d\n", options->nthread, options->qlen, options->delay);

  buf_dati->buf_len = options->qlen;
  buf_dati->head = 0;
  buf_dati->tail = 0;
  buf_dati->count = 0;
  buf_dati->buffer = (char **) malloc(buf_dati->buf_len*MAX_FILENAME_LEN);
  buf_dati->buf_lock = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
  buf_dati->not_full = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
  buf_dati->not_empty = (pthread_cond_t *) (sizeof(pthread_cond_t));
  options->delay = options->delay * 1000000; // 1 second = 1000000 microseconds (usleep prende in input i microsecondi)

  // inizializzazione mutex e condizioni
  pthread_t workers[options->nthread];
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  buf_dati->buf_lock = &mutex;
  pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
  buf_dati->not_full = &not_full;
  pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
  buf_dati->not_empty = &not_empty;
  
  // starto i workers
  for(int i=0; i<options->nthread; i++) {
    xpthread_create(&workers[i], NULL, &workerTask, buf_dati, QUI);
  }

  // legge i nomi dei file passati sulla linea di comando (skippa se il file non esiste)
  for (int i = 1; i < argc; i++) {
    if (sigs->gotSIG)
      break;
    assert(strlen(argv[i]) < MAX_FILENAME_LEN);
    // se il file non esiste, lo salta
    if ((access(argv[i], F_OK)) == -1) {
      //printf("File %s does not exist\n", argv[i]);
      continue;
    }
    // inserisce il nome del file nel buffer
    producer(buf_dati, argv[i]);
    if (i != argc-1)
      // all'ultimo inserimento nel buffer non faccio la sleep
      //printf("Sleeping for %d seconds...\n", options->delay/1000000);
      usleep(options->delay);
  }

  // invio poison pill a tutti i thread worker (segnale di terminazione)
  for(int i=0; i<options->nthread; i++) {
    producer(buf_dati, POISON_PILL);
  }

  // attendo terminazione worker threads
  for(int i=0;i<options->nthread;i++) {
      xpthread_join(workers[i], NULL, QUI);
  }

  pthread_cancel(handler);
  xpthread_join(handler, NULL, QUI);

  // libero le risorse
  xpthread_mutex_destroy(buf_dati->buf_lock, QUI);
  xpthread_cond_destroy(buf_dati->not_empty, QUI);
  xpthread_cond_destroy(buf_dati->not_full, QUI);
  free(options);
  free(buf_dati->buffer);
  free(buf_dati);
  free(sigs->mask);
  free(sigs);
  return 0;
}
