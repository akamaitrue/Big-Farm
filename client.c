#include "xerrori.h"
#include "utils.h"

struct sockaddr_in servaddr1;

void clientTask(int fd, int argc, char *argv[]);

int main(int argc, char *argv[]) {
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

    int w, tmp;
    char *msg = "Client";
    if (asprintf(&msg, "%s %d", msg, argc-1) < 0)
      xtermina("Errore asprintf", QUI);

    tmp = sizeof(msg);
    w = writen(fd, &tmp, sizeof(tmp));
    if (w != sizeof(int))
      xtermina("Errore scrittura su socket", QUI);
    //printf("Avvisato il socket che il messaggio sarà lungo %lu bytes\n", sizeof(msg));

    w = writen(fd, msg, sizeof(msg));
    if (w != sizeof(msg))
      xtermina("Errore scrittura su socket", QUI);
    //printf("Avvisato il socket che dovrà gestire %d richieste del tipo Client: <somma>\n", argc - 1);

    for (int i = 1; i < argc; i++) {
      // il messaggio deve essere del tipo: "Client: <somma>"

      //int a = isNumber(argv[i]); argv[i] = "4343r" => a=4343
      // con la funzione myIsNumber che ho definito, questo difetto latente non esiste
      if (myIsNumber(argv[i]) == 0) {
        fprintf(stderr, "[%d/%d] Errore: il parametro inserito (%s) non è un numero, skipping...\n", i, argc-1, argv[i]);
        continue;
      }
      int r, w, itmp, resSize;
      char *ires;
      char *msgToSend = "Client";
      if (asprintf(&msgToSend, "%s %s", msgToSend, argv[i]) < 0)
        xtermina("Errore allocazione memoria", QUI);

      itmp = strlen(msgToSend);
      w = writen(fd, &itmp, sizeof(itmp));
      if (w != sizeof(int))
        xtermina("Errore scrittura su socket", QUI);
      //printf("Avvisato il socket che il messaggio sarà lungo %d bytes\n", itmp);

      // invio la somma "Client: <argv[i]>"
      w = writen(fd, msgToSend, itmp);
      if (w != itmp)
        xtermina("Errore scrittura su socket", QUI);
      printf("[%d/%d] Sent message to server: %s\n", i, argc-1, msgToSend);

      r = readn(fd, &itmp, sizeof(itmp));
      if (r != sizeof(int))
        xtermina("Errore lettura da socket", QUI);

      resSize = ntohl(itmp);
      ires = (char *) malloc(resSize + 1);
      ires[resSize] = '\0';
      r = readn(fd, ires, resSize);
      if (r != resSize)
        xtermina("Errore lettura da socket", QUI);

      (void) ires; // avoid compiler warning
      printf("[%d/%d] Ricevuto somma: %s\n", i, argc-1, ires);
      free(msgToSend);
      free(ires);
    }
    free(msg);
  }
  else
  {
    // se invece il *Client* viene invocato senza argomenti sulla linea di comando, deve inviare una richiesta speciale al *Collector* di elencare tutte le coppie `somma`, `nomeFile` che lui ha ricevuto
    char msg[] = "Client liste";
    int r, w, tmp, resSize;
    char *res;

    tmp = sizeof(msg);
    w = writen(fd, &tmp, sizeof(tmp));
    if (w != sizeof(int))
      xtermina("Errore scrittura su socket", QUI);
    //printf("Avvisato il socket che il messaggio sarà lungo %lu bytes\n", sizeof(msg));


    w = writen(fd, msg, sizeof(msg));
    if (w != sizeof(msg))
      xtermina("Errore scrittura su socket", QUI);
    //printf("Sent message to server: %s\n", msg);

    r = readn(fd, &tmp, sizeof(tmp));
    if (r != sizeof(int))
      xtermina("Errore lettura da socket", QUI);

    resSize = ntohl(tmp);

    res = (char *) malloc(resSize + 1);
    res[resSize] = '\0';
    r = readn(fd, res, resSize);
    if (r != resSize)
      xtermina("Errore lettura da socket", QUI);

    (strcmp(res, "Nessun file") == 0) ? puts("Nessuna coppia (somma, nomeFile) presente nel Collector") : puts(res);

    free(res);
  }
}