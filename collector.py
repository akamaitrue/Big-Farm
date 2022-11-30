import os
import socket
import struct
import sys
import multiprocessing
import signal
from concurrent.futures import ThreadPoolExecutor

HOST = "127.0.0.1"
PORT = 57849
CORES = multiprocessing.cpu_count()
END_OF_COMMUNICATION = "__BYE__"

# lista di coppie (`somma`, `nomefile`) memorizzate dal *Collector*
somme = []

# Riceve esattamente n byte dal socket conn e li restituisce
# il tipo restituto è "bytes": una sequenza immutabile di valori 0-255
# Questa funzione è analoga alla readn che abbiamo visto nel C
def recv_all(conn,n):
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 1024))
    if len(chunk) == 0:
      raise RuntimeError("socket connection broken")
    chunks += chunk
    bytes_recd += len(chunk)
  return chunks


def sendPacked(conn, msg):
    msg = bytes(msg, "utf-8")
    res = struct.pack("%ds" % (len(msg)), msg)
    resSize = len(res)
    conn.sendall(struct.pack("!i", resSize))
    conn.sendall(res)

def recvPacked(conn):
    packedMsgLen = recv_all(conn, 4)
    msgLen = struct.unpack("I", packedMsgLen)[0]
    packedData = recv_all(conn, msgLen)
    msg = struct.unpack(f"!{msgLen}s", packedData)[0].decode()
    return msg

def identifyRequestType(req):
    return 1 if req.split()[0] == "Worker" else (2 if req.split()[0] == "Client" else 0)

def handleRequest(conn, addr):
    with conn:
        while (True):
            req = recvPacked(conn)
            if req == END_OF_COMMUNICATION or not req:
                break

            reqType = identifyRequestType(req)

            if reqType == 1:
                #* richiesta di tipo Worker: memorizza la coppia `somma`, `nomefile` e non risponde nulla
                nomefile, somma = req.split()[1:]
                somme.append((int(somma), nomefile))
                print(f"Memorizzata la coppia ({somma}, {nomefile})")

            elif reqType == 2:
                #* richiesta di tipo Client: il messaggio è del tipo "Client <somma>"

                request = req.split()[1].rstrip("\x00")
                if request.isdigit():
                    n = int(request)
                    for i in range(n):
                        msg = recvPacked(conn)
                        # "Client <somma>" -> la somma è la seconda parola del messaggio ricevuto
                        somma = int(msg.split()[1])
                        ris = []
                        for s, n in somme:
                            if s == somma:
                                ris.append((s, n))
                        ris.sort()
                        if (len(ris) == 0):
                            ris = f"Nessun file per la somma {somma}"
                        res = str(ris)
                        sendPacked(conn, res)

                # il messaggio è "Client liste": controlla se la seconda parola è "liste"
                elif request == "liste":
                    sommecpy = somme.copy()
                    res = ""
                    if len(sommecpy) == 0:
                        res = "Nessun file"
                        sendPacked(conn,res)
                    else:
                        sommecpy.sort()
                        res = res.join(str(s) + " " + n + "\n" for s, n in sommecpy)
                        sendPacked(conn,res)
                else:
                    #print(f"Invalid command from {addr}")
                    break
            else:
                # richiesta non valida
                res = "Invalid request"
                sendPacked(conn,res)
                #print(f"Invalid request from {addr}")
                break
            
        #print(f"Finito con {addr}")
        return


def main(host=HOST, port=PORT):
    gotSIG = False
    
    def exit_gracefully(signum, frame):
        gotSIG = True
        print(f" Received signal {signum}, exiting gracefully...")
        pool.shutdown(wait=True) # wait for currently active threads to finish
        server.shutdown(socket.SHUT_RDWR)
        server.close()
        sys.exit(0)


    # python3.8: signal.valid_signals() | https://docs.python.org/3/library/signal.html#signal.valid_signals
    for sig in signal.valid_signals():
        try:
            # wait for signal event then catch it and handle it with exit_gracefully function
            signal.signal(sig, exit_gracefully)
        except OSError:
            # SIGKILL and SIGSTOP are completely handled by Kernel hence cannot be caught, ignored or handled directly by a process
            # https://docs.python.org/3/library/signal.html
            pass

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    pool = ThreadPoolExecutor(max_workers=CORES*2)
    
    try:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen()

        while True:
            if gotSIG:
                # non accetto nuove connessioni
                break
            # mi metto in attesa di una connessione
            conn, addr = server.accept()
            # quando arriva una connessione, creo un thread che si occupa di gestirla
            pool.submit(handleRequest, conn, addr)

    except socket.error as e:
        print(f"Socket error: {e}")


if __name__ == "__main__":
    main()