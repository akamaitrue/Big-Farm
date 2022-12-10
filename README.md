# Big Farm

Progetto finale per il corso di Laboratorio II A.A. 2021/2022

## Descrizione del progetto

Il progetto richiede di realizzare una comunicazione client-server.
Per il Client ci sono due diversi eseguibili: farm.c e client.c\
`farm.c` è un eseguibile che prende da linea di comando un (lista di) file contenenti long, per ognuno ne calcola la somma e la invia al server.\
`client.c` interroga il server per sapere se ha memorizzate determinate coppie `(somma, nomefile)` e le stampa a video.
Il Server è un eseguibile python, `collector.py`. Il Collector memorizza le somme ricevute dal Farm e, su richiesta del client, restituisce determinate coppie (somma, nomefile).\
La descrizione fornita è estremamente sintetica e omette dettagli implementativi, approfondiamo adesso il funzionamento del programma e le principali scelte implementative.

![alt text](https://github.com/akamaitrue/Big-Farm/blob/main/gitignore/.lesson/schema-progettoBig.png)

## Farm

[Farm](https://github.com/akamaitrue/Big-Farm/blob/main/farm.c) prende 3 parametri opzionali da linea di comando: -n -q -d. Il parsing che ho implementato gestisce anche il caso in cui i parametri siano passati in ordine sparso, oppure quando lo stesso parametro viene specificato più volte: in ogni caso viene utilizzato l'ultimo parametro valido fornito. Se non vengono passati parametri opzionali oppure i parametri forniti sono invalidi, il programma utilizza i seguenti valori di default: `nthread=4 | qlen=8 | delay=0`\
L'unico vincolo sul passaggio dei parametri opzionali è che vengano forniti prima dei nomi dei file, rispettando il seguente formato\
`./farm -n 4 -q 12 -d 1 file.dat` se questo vincolo non è rispettato il programma viene comunque eseguito utilizzando i valori di default.\
Parametri opzionali, dati del buffer e variabili per la gestione dei segnali sono tutti gestiti all'interno di struct definite in [utils.h](https://github.com/akamaitrue/Big-Farm/blob/main/utils.h)

Dopo il parsing dei parametri, Farm inizializza le struct, threads, mutex e condition variables per la gestione della concorrenza.\
Le operazioni sui file passati da linea di comando sono delegate a dei thread worker, il cui numero è determinato dal parametro opzionale fornito in input (default 4), che lavorano secondo il paradigma master-slave: tutto il lavoro viene dunque delegato dal master ai workers.\
Nel master-slave implementato dal Big-Farm, il workflow è svolto nel seguente modo: c'è un buffer in cui il thread master inserisce i file e, una volta inseriti tutti, i thread worker li prelevano in mutua esclusione ed effettuano il calcolo della somma di ogni file per poi inviarla al Collector, che la memorizzerà come coppia nel formato (somma, nomefile).

![alt text](https://github.com/akamaitrue/Big-Farm/blob/main/gitignore/.lesson/somma3.png)

Prima di inserire i file nel buffer, il master effettua un controllo su tutti i parametri passati da linea di comando assicurandosi che siano file esistenti. Se il parametro non è un file o il file non esiste, lo salta\
L'inserimento e il prelievo dal buffer sono implementati con lo stile producer-consumer. L'accesso al buffer (sia in lettura che in scrittura) è gestito dalla variabile mutex su cui viene effettuata la lock/unlock, mentre le attese in caso di buffer pieno/vuoto sono implementate con le condition variables\
Dopo aver inserito i file nel buffer, il master invia un segnale di terminazione ai worker, in gergo "poison pill": la poison pill non è altro che una stringa inserita nel buffer dal thread master, che quando viene prelevata e letta dai worker triggera la loro terminazione\
La terminazione del programma può seguire dalla normale fine del workflow previsto oppure dalla ricezione di un segnale (SIGINT/SIGKILL/SIGSTOP), ma in ogni caso il programma termina quando tutti i thread sono terminati e tutte le risorse sono state rilasciate (distruzione di mutex e condition variables e liberare la memoria delle struct)

**Gestione SIGINT**: la gestione del segnale sigint è delegata a un thread handler.\
La maschera è configurata in modo che il SIGINT sia l'unico segnale per cui è garantita la terminazione pulita del programma (il testo non specifica niente riguardo alla gestione degli altri segnali), tuttavia ho scritto una riga che, se scommentata, permette di ignorare qualsiasi altro segnale annullandone l'effetto (tranne SIGKILL e SIGSTOP) in modo da evitare interruzioni brusche\
Se quest'ultimo è il comportamento che si preferisce per il programma, basta recarsi nel file [farm.c](https://github.com/akamaitrue/Big-Farm/blob/main/farm.c) e modificare le righe 21 e 22 nel modo seguente:

``` c
// con questa maschera posso ignorare tutti i segnali tranne SIGINT, SIGKILL e SIGSTOP
// ed eseguire l'handler per la terminazione pulita solo per SIGINT
sigemptyset(sigs->mask);
//sigaddset(sigs->mask, SIGINT);
```

Di default, comunque, il programma riceve qualsiasi segnale ma la terminazione pulita è solo per il SIGINT\
La terminazione consiste, come descritto per il farm, nell'inviare la poison pill ai thread worker (tramite il buffer), attendere la loro terminazione e liberare le risorse allocate

## Client

Il ruolo del [client](https://github.com/akamaitrue/Big-Farm/blob/main/client.c) è di ottenere informazioni dal Collector riguardo le somme memorizzate.\
Client e server (client.c e collector.py) si scambiano messaggi tramite socket, che in C non è altro che un file descriptor\
Il Client può inviare al Collector due tipi di messaggi:

* `Client somma1 somma2 ... sommaN-1 sommaN` per interrogare il Collector sull'eventuale presenza di associazioni (somma, nomefile) con una determinata somma\
In questo caso la comunicazione inizia con il Client che comunica al Collector il numero di somme che richiederà. Questa fase iniziale l'ho denominata clientHello e definito una funzione apposita\
Le somme passate da linea di comando sono controllate una ad una, ignorando quelle invalide. Per fare questo ho scritto una mia funzione custom (myIsNumber), che viene utilizzata anche dal Farm per il parsing dei paramtetri opzionali\
Dopo il messaggio di hello, il Client invia le somme al server tramite la stessa socket, in maniera sequenziale\
Il pattern per la comunicazione è il seguente: prima dell'invio della richiesta (response) effettiva, il client (server) invia al server (client) un intero che indica la lunghezza del messaggio, dopodiché si prosegue con l'invio della richiesta (response) vera e propria. Come si può intuire, questo pattern è utilizzato sia client-side che server-side\
Per ogni somma inviata, il client riceve in risposta dal server una (lista di) coppie (somma, nomefile) di file associati alla somma inviata, oppure nessun file se il Collector non ha trovato corrispondenze\
Ad ogni invio/ricezione si effettua la [conversione del messaggio da host a network order](https://manpages.ubuntu.com/manpages/bionic/man3/byteorder.3.html) (htonl prima dell'invio, ntohl dopo una ricezione). Lato server, invece, si converte con [struct.pack/unpack](https://docs.python.org/3/library/struct.html) \
Essendo operazioni molto frequenti in questo progetto, ho scritto delle funzioni per eseguire la conversione dei messaggi\

* L'altro tipo di operazione possibile per il Client è richiedere al Collector di inviare tutte le coppie (somma, nomefile) memorizzate. Per eseguire questa operazione ho stabilito un messaggio convenzionale `Client liste` riconosciuto anche dal Collector\
La risposta che riceve dal server può essere `Nessun file` oppure la lista di tutte le somme memorizzate, ordinate per somma crescente\

## Collector

[collector.py](https://github.com/akamaitrue/Big-Farm/blob/main/collector.py) implementa un server multithreaded che riceve ed elabora le richieste provenienti da Farm e Client.
Per realizzare il multithreading ho scelto di utilizzare una thread pool anziché lanciare un nuovo thread per ogni richiesta. \
Ogni richiesta in entrata viene assegnata ad un thread, che la elabora come task. La particolarità sta nel fatto che la pool impone un upper bound al numero di thread che possono essere creati: la pool size che ho scelto equivale al doppio del numero di processori che possiede la macchina su cui si sta eseguendo il programma. Questo valore viene dunque calcolato a runtime \
Il motivo per cui ho scelto di usare una pool anziché creare nuovi thread on-demand per ogni connessione è quello di prevenire abusi da parte del client, che potrebbe spammare richieste e il server di conseguenza riempirebbe lo heap molto in fretta creando threads a profusione\Per l'uso accademico del Big-Farm sarebbe stata sufficiente la creazione di nuovi thread on-demand per ogni richiesta, ma non sarebbe di certo una soluzione scalabile per grandi quantità di richieste. Una thread pool che gestisce le richieste in modo asincrono e riutilizzando un thread per più richieste è già una soluzione più plausibile e applicabile a scenari reali

Ogni richiesta che il Collector riceve è un messaggio di cui prima viene ricevuta la lunghezza, dopodiché arriva il messaggio effettivo contenente la richiesta da elaborare. Dato che lo scambio di messaggi è un'operazione molto frequente in questo progetto, ho scritto due funzioni ausiliari per eseguire la conversione dei messaggi: `recvPacked()` e `sendPacked()` che rendono i dati leggibili e pronti all'uso per le manipolazioni che farà il Collector

Per distinguere le richieste in entrata, ho implementato un protocollo molto semplice che consiste nella decodifica del messaggio ricevuto per poi splittarlo e considerare solo la prima parola. Se il risultato di queste operazioni è la stringa `Client`, allora il messaggio proviene dal processo Client e il Collector esegue la routine assegnata a quel tipo di richieste, se invece il risultato è `Worker` significa che la richiesta proviene dal processo Farm e il thread esegue la routine specificata per quel tipo di richiesta\
Il server, a seconda del messaggio che riceve, svolge le seguenti routine:

* `Worker somma nomefile` è il formato dei messaggi inviati dal processo Farm. Il server memorizza la coppia (somma, nomefile) senza inviare una risposta al client, oppure ignora la richiesta se un file con lo stesso nome è già presente nel collector
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.38.53.jpg)
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.39.59.jpg)

* `Client somma1 somma2 sommaN-1 sommaN` in questo caso il Collector verifica, per ogni somma specificata nel messaggio, se nella lista compaiono coppie (somma, nomefile) corrispondenti a quella somma e, se presenti, invia le coppie come risposta. In caso non ce ne fossero, invia la stringa "Nessun file" specificando la somma a cui si riferisce\
Ho deciso di assegnare un unico messaggio di risposta ad ogni connessione, in quanto per un grande numero di occorrenze trovate sarebbe poco conveniente inviare un messaggio al client per ogni coppia (somma, nomefile) individuata\
Per quanto riguarda la struttura dati utilizzata per memorizzare le somme, inizialmente nelle prime versioni ho optato per un dictionary che poi ho sostituito con una lista in quanto nel testo viene specificato così
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.41.49.jpg)

* `Client liste` è il messaggio con cui il Client richiede di elencare tutte le coppie (somma, nomefile) memorizzate. In questo caso il Collector crea una copia dell'intera lista, esegue una sort per somma crescente e la invia in risposta al client. Qualora invece la lista fosse vuota, la risposta è semplicemente la stringa "Nessun file"
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.42.15.jpg)
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.47.32.jpg)

Il server non termina mai spontaneamente, la terminazione è triggerata dalla ricezione di un segnale.
Anche per il Server è presente la gestione del segnale SIGINT ma, a differenza di Farm, per il Collector ho scelto di far gestire [tutti i segnali](https://docs.python.org/3/library/signal.html#signal.valid_signals) all'handler, [tranne SIGKILL e SIGSTOP](https://docs.python.org/3/library/signal.html).
Ho scelto di [far terminare in maniera pulita](https://elearning.di.unipi.it/mod/forum/discuss.php?d=4447) per tutti i segnali perché il Collector è il processo che si occupa di salvare i dati, quindi è importante che venga terminato in maniera pulita per perdite di dati
![alt text](https://github.com/akamaitrue/dummy-repo/blob/main/Screenshot%202022-12-09%20alle%2022.50.39.jpg)

### Utility functions

Il cuore implementativo del progetto (lato client) si trova all'interno di [questo file](https://github.com/akamaitrue/Big-Farm/blob/main/utils.c), che contiene funzioni ausiliarie per svolgere la maggior parte delle operazioni descritte in precedenza
Ho cercato di alleggerire il più possibile Farm e Client implementando funzioni di utilità in `utils.c` per rendere il codice più pulito e apprezzabile
