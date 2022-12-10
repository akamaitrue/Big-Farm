/*
* @author: Francesco Massellucci
* @brief: Progetto Big-Farm per il corso di Laboratorio II A.A. 2021/2022
* @file: client.c
* @date: 08/12/2022
* @version: 2.5
*/

#include "xerrori.h"
#include "utils.h"


void clientTask(int fd, int argc, char *argv[]);

int main(int argc, char *argv[])
{
  struct sockaddr_in servaddr1;
  int sockfd;
  // from man page: -1 is returned if an error occurs, otherwise the return value is a descriptor referencing the socket.
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		xtermina("Errore creazione socket", QUI);
  //puts("Socket creata con successo");

	// assign IP, PORT
	servaddr1.sin_family = AF_INET;
	servaddr1.sin_addr.s_addr = inet_addr(IP);
	servaddr1.sin_port = htons(PORT);

	if (connect(sockfd, (SA*)&servaddr1, sizeof(servaddr1)) < 0)
        xtermina("Connessione fallita", QUI);
  //puts("Connessione al server riuscita");

  // timeout di 3 secondi per la ricezione delle risposte dal server
  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0)
    xtermina("Errore setsockopt", QUI);

  clientTask(sockfd, argc, argv); 
	if (close(sockfd) < 0)
    perror("Errore chiusura socket\n");  
  return 0;
}

void clientTask(int fd, int argc, char *argv[])
{
  // prende come input sulla linea di comando una sequenza di `long`
  if (argc > 1)
  {
    // per ognuno di essi chiede al *Collector* se ha ricevuto dal *MasterWorker* nomi di file con associato quella somma

    char *msg = "Client";
    if (asprintf(&msg, "%s %d", msg, argc-1) < 0)
      xtermina("Errore allocazione memoria (asprintf)", QUI);

    // comunico in anticipo al server il numero di somme che dovrà gestire
    clientHello(fd, msg);

    for (int i = 1; i < argc; i++) {
      // il messaggio è del tipo: "Client: <somma>"

      if (myIsNumber(argv[i]) == 0) {
        fprintf(stderr, "[%d/%d] Errore: il parametro inserito (%s) non è un numero, skipping...\n", i, argc-1, argv[i]);
        continue;
      }

      char *msgToSend = "Client";
      if (asprintf(&msgToSend, "%s %s", msgToSend, argv[i]) < 0)
        xtermina("Errore allocazione memoria (asprintf)", QUI);

      sendMsg(fd, msgToSend);
      printf("[%d/%d] Sent message to server: %s\n", i, argc-1, msgToSend);
      
      // Ricezione risposta
      char *res;
      res = getResponse(fd);
      printf("[%d/%d] %s\n", i, argc-1, res);

      free(res);
      free(msgToSend);
    }
    free(msg);
  }
  else
  {
    // se invece il *Client* viene invocato senza argomenti sulla linea di comando, deve inviare una richiesta speciale al *Collector* di elencare tutte le coppie `somma`, `nomeFile` che lui ha ricevuto
    char msg[] = "Client liste";
    char *res;

    sendMsg(fd, msg);
    printf("Sent message to server: %s\n", msg);

    // Ricezione risposta
    res = getResponse(fd);
    (strcmp(res, "Nessun file") == 0) ? puts("Nessuna coppia (somma, nomeFile) presente nel Collector") : puts(res);

    free(res);
  }
}