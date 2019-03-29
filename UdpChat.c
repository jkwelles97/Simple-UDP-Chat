#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define PRINTPACKETS 0 // set to 1 and the chat members will print the packets they recieve
#define QUEUESIZE 10 // number of outgoing un-ACKed messages a client can have
#define BUFFSIZE 1000 // BUFFSIZE = maxMessageSize + 250 characters
#define PROPERUSE "Proper Usage:\nClient: UdpChat -c <nick-name> <server-ip> <server-port> <client-port>\nServer: UdpChat -s <port>\nExiting\n"


int* tableSize;
int* fullSlots;

// prints the chat prompt
void prompt()
{
  fprintf(stderr, "$>>>");
}

// used for error exits only
void die(char* s)
{
  perror(s);
  exit(0);
}


// the packets sent between users
// has form type[tab]source[tab]dest[tab]content
struct packet
{
  char type[4];
  char source[48];
  char dest[48];
  char content[BUFFSIZE - 100];
};

// entry in table of active users
struct tableEntry
{
  char name[49];
  char IP[17];
  int port;
  int status;
};

// global packets and entries used for sending + recieving
struct tableEntry** table;
struct packet* recPacket;
struct packet* sendPacket;
struct tableEntry* entry;

// functions for handling signals
void cleanExit(){exit(0);}
int keepRunning = 1;
void intHandler() {keepRunning = 0;}
int sendPermission = 1;
void permHandler() {sendPermission = 1;}

// frees mallocs()
void freeTable()
{
  int i = 0;
  for(i = 0; i < *tableSize; i++)
  {
    free(table[i]);
  }
  free(table);
  free(tableSize);
  free(fullSlots);
  free(recPacket);
  free(sendPacket);
  free(entry);
}

// exits and frees tables
void freeExit(){freeTable();exit(0);}

// sends a single packet to the ip and port listed
void sendMessage(struct packet* packet, char* IP, int port)
{
  struct sockaddr_in serv_in;
  int s;
  char buf[BUFFSIZE];
  sprintf(buf, "%s\t%s\t%s\t%s\t", packet->type, packet->source, packet->dest, packet->content);
  if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    die("Socket Error");

  memset(&serv_in, 0, sizeof(struct sockaddr_in));
  serv_in.sin_family = AF_INET;
  serv_in.sin_port = htons(port);
  if (inet_aton(IP, &serv_in.sin_addr)==0)
  {
    perror("inet_aton() failed");
    freeExit();
  }
  int size = sizeof(serv_in);
  int msgSize = strlen(buf) + 1;
  if (sendto(s, buf, msgSize, 0, (const struct sockaddr*)&serv_in, size) == -1)
    die("sendto() failed");
  close(s);
  return;
}

// makes a packet from constituent strings
void makePacket(struct packet* packet, char* type, char* source, char* dest, char* content)
{
  memcpy(packet->type, type, sizeof(packet->type));
  memcpy(packet->source, source, sizeof(packet->source));
  memcpy(packet->dest, dest, sizeof(packet->dest));
  memcpy(packet->content, content, sizeof(packet->content));
}

// makes a table entry from different strings
void makeEntry(struct tableEntry* myEntry, char* name, char* ip, int port, int status)
{
  memcpy(myEntry->name, name, sizeof(myEntry->name));
  memcpy(myEntry->IP, ip, sizeof(myEntry->IP));
  myEntry->port = port;
  myEntry->status = status;
}

// extracts a packet from an incoming string
void extractPacket( struct packet* packet, char* message)
{
  char buf[BUFFSIZE];
  sprintf(buf, "%s", message);
  char* delims = "\t";
  char* type =  strtok(buf, delims);
  char* source =  strtok(NULL, delims);
  char* dest = strtok(NULL, delims);
  char* content = strtok(NULL, delims);
  makePacket(packet, type, source, dest, content);
}

// extracts an entry from the contents of a packet
void extractEntry(struct tableEntry* myEntry, char* message)
{
  char buf[BUFFSIZE];
  sprintf(buf, "%s", message);
  char* delims = " ";
  char* name = strtok(buf, delims);
  char* ip = strtok(NULL, delims);
  int port = atoi(strtok(NULL, delims));
  int status = atoi(strtok(NULL, delims));
  makeEntry(myEntry, name, ip, port, status);
}

// makes an update message to be sent with a packet =
void makeUpdateMessage(char *message, struct tableEntry* myEntry)
{
  sprintf(message, "%s %s %d %d", myEntry->name, myEntry->IP, myEntry->port, myEntry->status);
}

// update the entry table given the string message
// name is given so that your name isn't printed when updating
// speak can be set to 0 if you don't want this function to print anything
void updateTable(char* message, char* name, int speak)
{
  struct tableEntry* myEntry = (struct tableEntry*) malloc( sizeof(struct tableEntry));
  int i= 0;
  extractEntry(myEntry, message);
  for (i = 0; i < *fullSlots; i++)
  {
    if(strcmp(table[i]->name, myEntry->name) == 0)
    {
      int status = table[i]->status;
      if(status == myEntry->status||strcmp(myEntry->IP, table[i]->IP) != 0|| myEntry->port != table[i]->port)
      {
        return;
        free(myEntry);
      }
      free(table[i]);
      table[i] = myEntry;
      if(speak== 1 && strcmp(name, myEntry->name) != 0)
      {
        if(status == 1)
        {
          printf("[User %s has left the channel]\n", myEntry->name);
          prompt();
        }
        if(status == 0)
        {
          printf("[User %s has rejoined the channel]\n", myEntry->name);
          prompt();
        }
      }
      return;
    }
  }
  if(*fullSlots + 1 > *tableSize)
  {
    if(realloc(table, sizeof(struct tableEntry*) * (*tableSize+5)) == NULL)
      die("realloc failed");
    *tableSize = *tableSize+5;
  }
  free(table[*fullSlots]);
  table[*fullSlots] = myEntry;
  *fullSlots = *fullSlots + 1;

  if(speak == 1 && strcmp(name, myEntry->name) != 0)
  {
    if(myEntry->status == 0)
    {
      printf("[User %s(OFFLINE) is in the channel]\n", myEntry->name);
    }
    else
    {
      printf("[User %s has joined the channel]\n", myEntry->name);
    }
    prompt();
  }
  return;
}

//functions query different aspects of the table by name
char* queryTableIP(char* name)
{
  int i = 0;
  for(i = 0; i < *tableSize; i++)
  {
    if(strcmp(table[i]->name, name) == 0)
      return (char*)table[i]->IP;
  }
  return NULL;
}
int queryTablePort(char* name)
{
  int i = 0;
  for(i = 0; i < *tableSize; i++)
  {
    if(strcmp(table[i]->name, name) == 0)
      return table[i]->port;
  }
  return -1;
}
int queryTableStatus(char* name)
{
  int i = 0;
  for(i = 0; i < *tableSize; i++)
  {
    if(strcmp(table[i]->name, name) == 0)
      return table[i]->status;
  }
  return -1;
}

// listens for packets on clnt port and sends them to its parent through channel
// pid is the id of this processes child, which scans stdin
void listeningChild( int clntPort, int channel, pid_t pid)
{
  int s;
  int msgSize;
  char buffer[BUFFSIZE];
  struct sockaddr_in me_in;
  signal(SIGINT, SIG_IGN); //The child needs to keep recieving packets after parent gets SIGINT
  signal(SIGUSR1, permHandler); //will set sendPermission to 1
  signal(SIGUSR2, cleanExit); //will set childLive to 0
  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
    die("Socket Error");

  memset(&me_in, 0, sizeof(struct sockaddr_in));
  me_in.sin_family = AF_INET;
  me_in.sin_port = htons(clntPort);
  me_in.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (const struct sockaddr*) &me_in, sizeof(me_in))==-1)
  {
    die("bind() failed");
  }
  while(1)
  {
    recvfrom(s, buffer, BUFFSIZE, 0,NULL, NULL);
    while(sendPermission == 0){} // wait for the client to be ready to read
    msgSize = strlen(buffer)+1;
    write(channel,(char*)buffer, msgSize);
    sendPermission = 0;
  }
}

// a list entry for the list of outgoing messages waiting ack
struct killTimer
{
  clock_t time;
  int failures;
  char message[BUFFSIZE];
  char dest[49];
  int thruServ;
};

// prints the killqueue
void printKillq(struct killTimer* killq, int items)
{
  int i =0;
  for(i = 0; i < items; i++)
  {
    printf("%s %s %d\n", killq[i].dest, killq[i].message, items);
  }
}

// updates list of pending transactions
// dest is the user who was sent the message, or "server" if waiting for server ack
// message is the message they were sent
// remove is either 0 or 1 whether we are adding items or removing them
// thruServ is a bit of a cheat, if the client is waiting for ACK from server
// of a saved message, dest will actually be from the user who the message was intended for
// rather than "server". thruServ lets the function know when something is going
// "through the server" rather than an actual ACK from the user in dest
int updateKillq(struct killTimer* killq,int items, char* dest,char* message, int remove, int thruServ)
{
  int k = items;
  if(k+1 > QUEUESIZE)
  {
    printf("[You have too many pending messages! Wait for a few to time out.]\n");
    prompt();
    return -1;
  }
  int i = 0;
  for(i = 0; i < items; i++)
  {
    if(strcmp(killq[i].dest, dest) == 0)
    {
      if(remove == 1)
      {
         int j;
         if(items >  i +1)
         {
           for(j = i; j < items-1; j++)
           {
             memcpy(&(killq[j]), &(killq[j+1]), sizeof(struct killTimer));
           }
         }
         k--;
      }
      else
      {
        printf("[There is already a pending message to this target, wait a moment]\n");
        prompt();
        return -1;
      }
    }
  }
  if(remove == 1 && k == items)
  {
    return -1;
  }
  if(remove == 1)
    return k;
  memcpy(killq[k].dest, dest, 25);
  memcpy(killq[k].message, message, BUFFSIZE);
  killq[k].time = clock();
  killq[k].failures = 0;
  killq[k].thruServ = thruServ;
  k++;
  return k;
}
// the function for client activities
void client(char* name, char* servIP, int servPort, int clntPort)
{
  int ppipe[2]; //packet pipe
  int upipe[2]; //user pipe
  pipe(ppipe);
  pipe(upipe);
  pid_t pid = fork(); //fork once
  if(pid < 0)
    die("fork failed");
  if(pid == 0)
  {
    //child process
    close(ppipe[0]);
    close(upipe[0]);
    pid = fork(); //fork again
    if (pid < 0)
      die("fork failed");
    if(pid !=  0)
    {
      //listens for packets
      close(upipe[1]);
      listeningChild(clntPort, ppipe[1], pid);
    }
    else
    {
      //listens for user input
      close(ppipe[1]);
      int ctr = 0;
      char buffer[BUFFSIZE];
      while(1)
      {
        char c;
        while((BUFFSIZE-250)-1 > ctr && (c = getc(stdin)) != '\n')
        {
          buffer[ctr] = c;
          ctr++;
        }
        buffer[ctr]  = '\0';

        write(upipe[1], buffer, strlen(buffer)+1);
        if(ctr == (BUFFSIZE-250)-1)
        {
           while(getc(stdin) != '\n'){}
        }
        ctr = 0;
      }
    }
  }
  else
  {
    // this is the main parent, does all the analysis and sending
    signal(SIGINT, intHandler);
    struct killTimer killq[QUEUESIZE];
    int items = 0;
    int startup = 1;
    int reged = 0;

    //mallocs the table and associated items
    fullSlots = (int*) malloc(sizeof(int));
    tableSize = (int*) malloc(sizeof(int));
    *fullSlots = 0;
    *tableSize = 10;
    table = (struct tableEntry **) malloc(sizeof(struct tableEntry*)* *tableSize);
    int i = 0;
    for(i= 0; i < *tableSize; i++)
    {
      table[i] =  (struct tableEntry *) malloc(sizeof(struct tableEntry));
    }


    close(ppipe[1]);
    close(upipe[1]);
    FILE* packets = fdopen(ppipe[0],"r");
    FILE* usrinput = fdopen(upipe[0], "r");
    int fd;
    int qFlag = 0;
    char buffer[BUFFSIZE];
    char regMessage[BUFFSIZE];
    struct ifreq ifr;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    char *myIP = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    entry = (struct tableEntry*) malloc(sizeof(struct tableEntry));
    recPacket = (struct packet*) malloc (sizeof(struct packet));
    sendPacket = (struct packet*) malloc (sizeof(struct packet));
    makeEntry(entry, name, myIP, clntPort, 1);
    makeUpdateMessage(regMessage, entry);
    clock_t start = clock();
    //this waits a second to let the children and signals setup properly
    while((clock() - start) < CLOCKS_PER_SEC){}
    while(1)
    {
      if(startup)
      {
        prompt();
        printf("[Welcome to UDPchat]\n");
        prompt();
        makePacket(sendPacket, "REG", name, "server", regMessage);
        items = updateKillq(killq, items, "server", regMessage, 0,0);

        //sends a reg request automatically
        sendMessage(sendPacket, servIP, servPort);
        if(items < 0)
          die("somethings wrong");
        startup = 0;
      }


      struct pollfd pck;
      pck.fd = ppipe[0];
      pck.events = POLLIN;
      if(poll(&pck, 1, 0) == 1)
      {
        //this means there is a packet to be read
        char c;
        int count = 0;
        while((c = fgetc(packets)) != '\0' && c != EOF)
        {
          if(count == BUFFSIZE)
            die("oversized packet");
          buffer[count] = c;
          count++;
        }
        kill(pid, SIGUSR1);
        if(c == EOF)
        {
          freeExit();
          //this means the packet child has died, we can't continue
        }
        buffer[count] = '\0';
        if(c != EOF && PRINTPACKETS)
        {
          printf("GOT PACKET: %s\n", buffer);
          prompt();
        }
        extractPacket(recPacket, buffer);

        // deals with ACKS
        if(strncmp(recPacket->type, "ACK",3) == 0)
        {
          int i = updateKillq(killq, items, recPacket->source, NULL, 1,0);
          if(i >= 0)
          {
            if(strcmp(recPacket->content, "dereg") == 0)
            {
              //dereg ACK
              printf("[You are deregistered]\n");
              prompt();
              if(keepRunning == 0)
              {
                printf("[Exiting]\n");
                kill(pid, SIGUSR1);
                kill(pid, SIGUSR2);
                freeExit();
              }
              reged = 0;
            }
            else if(strcmp(recPacket->content, "offline")==0)
            {
              //offline message storage ACK
              printf("[Message recieved and stored by server]\n");
              prompt();
            }
            items = i;
          }
        }

        // These are offline messages, they are just printed as the server does the formatting
        if(strncmp(recPacket->type, "OFL",3) == 0)
        {
          printf("%s", recPacket->content);
          prompt();
        }

        // if we recieve a message we print it
        if(strncmp(recPacket->type, "MSG",3)  == 0)
        {
          printf("Message from %s: %s\n", recPacket->source, recPacket->content);
          prompt();
          makePacket(sendPacket, "ACK", name, recPacket->source, "ACK");
          if(queryTableStatus(recPacket->source) > 0)
            sendMessage(sendPacket, queryTableIP(recPacket->source), queryTablePort(recPacket->source));
        }

        // UPD packets are updates to the table registry
        // they are used as ACKs for registrations requests
        if(strncmp(recPacket->type, "UPD",3) == 0)
        {
          if(reged == 0)
          {
            printf("[You are registered]\n");
            prompt();
          }
          updateTable(recPacket->content,name, 1);
          if(queryTableStatus(name) == 0)
          {
            printf("[Client Dropped Packets. Exiting]\n");
            prompt();
            // When exiting we have to do all this, the two kills give the
            // exit signal, and then we send a packet to break the packet child
            // out of the recv loop
            kill(pid, SIGUSR1);
            kill(pid, SIGUSR2);
            freeExit();
          }
          int i = updateKillq(killq, items, "server", NULL, 1,0);
          if(i >= 0)
          {
            reged = 1;
            items = i;
            printKillq(killq, items);
          }
        }
      }


      struct pollfd usr;
      usr.fd = upipe[0];
      usr.events = POLLIN;
      if(poll(&usr,1,0)==1)
      {
        // this handles user input
        char c;
        int count = 0;
        while((c = fgetc(usrinput)) != '\0' && c != EOF)
        {
          if(count == BUFFSIZE)
            die("oversized message");
          buffer[count] = c;
          count++;
        }
        buffer[count] = '\0';
        if(c!=EOF)
        {
          prompt();
        }
        char* delims = " ";
        char* task = strtok(buffer, delims);
        char* dest = strtok(NULL, delims);
        char message[BUFFSIZE - 100];
        int ctr = 0;
        char* temp;
        while((temp = strtok(NULL, delims)) != NULL)
        {
          memcpy((char*)(message + ctr), temp, strlen(temp)*sizeof(char));
          ctr = ctr + strlen(temp);
          if(ctr < BUFFSIZE-100)
          {
            message[ctr] = ' ';
            ctr++;
          }
        }
        if(ctr > 0)
          message[ctr-1] = '\0';
        else
          message[ctr] = '\0';
        if(c == EOF|| count == 0)
          task = "wait"; //wait just fails all the checks so nothing happens
        if(strcmp(task, "send") == 0 && dest != NULL)
        {
          // handles the send command
          if(reged == 1)
          {
            if(queryTableStatus(dest) == 0)
            {
              printf("[User %s is offline, sending message to server]\n", dest);
              prompt();
              int i = updateKillq(killq, items, dest, message, 0,1);
              if(i > 0)
              {
                items = i;
                makePacket(sendPacket,"MSG", name, dest, message);
                sendMessage(sendPacket, servIP, servPort);
              }
            }
            else if(queryTableStatus(dest) == 1)
            {
              int i = updateKillq(killq, items, dest, message, 0,0);
              if(i > 0)
              {
                items = i;
                makePacket(sendPacket,"MSG", name, dest, message);
                sendMessage(sendPacket, queryTableIP(dest), queryTablePort(dest));
              }
            }
            else
            {
              printf("[There is no user with that name]\n");
              prompt();
            }
          }
          else
          {
            printf("[Cannot send messages while unregistered]\n");
            prompt();
          }
        }
        else if(strcmp(task, "reg") == 0)
        {
          // handles the reg command
          if(reged == 1)
          {
            printf("[You are already registered]\n");
            prompt();
          }
          else
          {
            int i = updateKillq(killq, items, "server", regMessage, 0,0);
            if(i > 0)
            {
              items  = i;
              makePacket(sendPacket,"REG", name, "server", regMessage);
              sendMessage(sendPacket, servIP, servPort);
            }
          }
        }
        else if(strcmp(task, "dereg") == 0)
        {
          // handles the dereg command
          if(reged == 0)
          {
           printf("[You are not registered]\n");
           prompt();
          }
          else
          {
            int i = updateKillq(killq, items, "server", "dereg",0,0);
            if( i > 0)
            {
              printf("[Sending dereg signal]\n");
              prompt();
              items = i;
              makePacket(sendPacket,"DRG", name, "server", "dereg");
              sendMessage(sendPacket, servIP, servPort);
            }
          }
        }
        else if(strcmp(task, "wait") != 0)
        {
          printf("[Command not recognized]\n");
          prompt();
        }
      }


      // this is triggered when we get SIGINT
      if(keepRunning == 0)
      {
        int r;
        if(reged==0)
        {
          printf("[Exiting]\n");
          kill(pid, SIGUSR1);
          kill(pid, SIGUSR2);
          freeExit();
        }
        else
        {
          if(qFlag == 0 && (r = updateKillq(killq, items, "server", "dereg", 0,0)) > 0)
          {
            items = r;
            printf("[Sending dereg signal]\n");
            prompt();
            makePacket(sendPacket, "DRG", name, "server", "dereg");
            sendMessage(sendPacket, servIP, servPort);
            qFlag = 1;
          }
        }
      }

      // this loop handles the queue of waiting ACKS
      int l = 0;
      for(l = 0; l < items; l++)
      {
        if(((((double)(clock() - killq[l].time))/CLOCKS_PER_SEC)*1000) >  500)
        {
          // a message has timed out
          killq[l].failures++;
          killq[l].time = clock();
          if(killq[l].failures > 4)
          {
            // a message has failed 5 times and we must take action
            if(strcmp(killq[l].dest, "server") == 0|| killq[l].thruServ == 1)
            {
              // connection failures to server
              // packets used for offline messaging are not signed from
              // server so we use thruServ to tell the difference
              if(strcmp(killq[l].message, "dereg") == 0 || reged == 1)
                printf("[Server not responding. Exiting]\n");
              else
                printf("[Server not responding. Name may already be taken. Exiting]\n");
              kill(pid, SIGUSR1);
              kill(pid, SIGUSR2);
              freeExit();
            }
            else
            {
               // connection failures to user
               printf("[User %s not responding, sending message to server]\n", killq[l].dest);
               prompt();
               items  = updateKillq(killq, items, killq[l].dest, NULL, 1, 0);
               int i = updateKillq(killq, items, killq[l].dest, killq[l].message, 0, 1);
               if(i > 0)
               {
                 items = i;
                 makePacket(sendPacket,"MSG", name, killq[l].dest, killq[l].message);
                 sendMessage(sendPacket, servIP, servPort);
               }
            }
          }
          else
          {
            // we have not failed too many times
            if(strcmp(killq[l].dest, "server") == 0)
            {
              if(strcmp(killq[l].message, "dereg") == 0)
              {
                makePacket(sendPacket, "DRG", name, killq[l].dest, killq[l].message);
              }
              else
              {
                makePacket(sendPacket, "REG", name, killq[l].dest, killq[l].message);
              }
              sendMessage(sendPacket, servIP, servPort);
            }
            else
            {
              makePacket(sendPacket, "MSG", name, killq[l].dest, killq[l].message);
              if(killq[l].thruServ ==1)
              {
                sendMessage(sendPacket, servIP, servPort);
              }
              else
              {
                sendMessage(sendPacket, queryTableIP(killq[l].dest), queryTablePort(killq[l].dest));
              }
            }
          }
        }
      }
    }
  }
}
//sends the info table to all active clients
void sendTables(struct packet* packet)
{
  char buffer[BUFFSIZE];
  int i = 0;
  for(i = 0; i < *fullSlots; i++)
  {
    if(table[i]->status == 1)
    {
      int j;
      for(j = 0; j < *fullSlots; j++)
      {
        makeUpdateMessage(buffer, table[j]);
        makePacket(packet, "UPD", "server", table[i]->name, buffer);
        sendMessage(packet, queryTableIP(table[i]->name), queryTablePort(table[i]->name));
      }
    }
  }
}
//reads from a log if it exists, sends log packets and closes the log
void sendOfl(struct packet* myPacket, char* name)
{
  char buf[BUFFSIZE-100];
  char filename[100];
  sprintf(filename, "%s.log", name);
  if(access(filename, F_OK) != -1)
  {
    FILE * temp = fopen(filename, "r");
    makePacket(myPacket, "OFL", name, "server", "You have messages:\n");
    sendMessage(myPacket, queryTableIP(name), queryTablePort(name));
    while(fgets(buf, sizeof(buf), temp) != NULL)
    {
      makePacket(myPacket, "OFL", name, "server", buf);
      sendMessage(myPacket, queryTableIP(name), queryTablePort(name));
    }
    if(fclose(temp) == EOF)
      perror("close failed");

    if(remove(filename) < 0)
      perror("remove failed");
  }
}

//server function
void server(int port)
{
  //malloc necessary structures
  fullSlots = (int*) malloc(sizeof(int));
  tableSize = (int*) malloc(sizeof(int));
  *fullSlots = 0;
  *tableSize = 10;
  table = (struct tableEntry **) malloc(sizeof(struct tableEntry*)* *tableSize);
  int i = 0;
  for(i= 0; i < *tableSize; i++)
  {
    table[i] =  (struct tableEntry *) malloc(sizeof(struct tableEntry));
  }

  signal(SIGINT, freeExit);

  struct sockaddr_in serv_in;
  int s;
  char buf[BUFFSIZE];

  //setup the socket
  if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
    die("Socket Error");
  memset(&serv_in, 0, sizeof(struct sockaddr_in));
  serv_in.sin_family = AF_INET;
  serv_in.sin_port = htons(port);
  serv_in.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (const struct sockaddr*) &serv_in, sizeof(serv_in))==-1)
    die("bind() failed");
  struct tableEntry* entry = (struct tableEntry*) malloc(sizeof(struct tableEntry));
  struct packet* recPacket = (struct packet*) malloc(sizeof(struct packet));
  struct packet* sendPacket = (struct packet*) malloc(sizeof(struct packet));

  prompt();
  while(1)
  {
    recvfrom(s, buf, BUFFSIZE, 0, NULL, NULL);
    if(PRINTPACKETS)
    {
      printf("GOT PACKET: %s\n", buf);
      prompt();
    }
    extractPacket(recPacket, buf);
    if(strncmp(recPacket->type, "REG",3)==0)
    {
      // client asks to be reged, table is updated log is sent if there is one
      updateTable(recPacket->content,"server",1);
      sendTables(sendPacket);
      sendOfl(sendPacket, recPacket->source);
    }
    if(strncmp(recPacket->type, "DRG", 3)==0  && queryTableStatus(recPacket->source) != -1)
    {
      // client asks to be dereged, update the tables and send them to clients
      makePacket(sendPacket, "ACK", "server", recPacket->source, "dereg");
      sendMessage(sendPacket, queryTableIP(recPacket->source), queryTablePort(recPacket->source));
      makeEntry(entry, recPacket->source, queryTableIP(recPacket->source), queryTablePort(recPacket->source), 0);
      char message[BUFFSIZE];
      makeUpdateMessage(message, entry);
      updateTable(message,"server",1);
      sendTables(sendPacket);
    }
    if(strncmp(recPacket->type, "MSG", 3) == 0 && queryTableStatus(recPacket->source) > 0)
    {
      // reead offline message and store it in a log file
      makePacket(sendPacket, "ACK", recPacket->dest, recPacket->source, "offline");
      sendMessage(sendPacket, queryTableIP(recPacket->source), queryTablePort(recPacket->source));
      makeEntry(entry, recPacket->dest, queryTableIP(recPacket->dest), queryTablePort(recPacket->dest), 0);
      char message[BUFFSIZE];
      makeUpdateMessage(message, entry);
      updateTable(message, "server",1);
      sendTables(sendPacket);
      char filename[100];
      char timeStamp[75];
      char logMessage[BUFFSIZE-100];
      FILE* temp;
      sprintf(filename, "%s.log", recPacket->dest);
      if(access(filename, F_OK) == -1)
      {
        temp = fopen(filename, "w");
      }
      else
      {
        temp = fopen(filename, "a");
      }
      if(temp == NULL)
        die("fopen failed");
     time_t t = time(NULL);
     struct tm tm = *localtime(&t);
     sprintf(timeStamp,"%d-%d-%d %d:%d:%d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour-5, tm.tm_min, tm.tm_sec);
     sprintf(logMessage,"%s: <%s> %s\n", recPacket->source, timeStamp, recPacket->content);
     if(strlen(logMessage) != fwrite(logMessage, 1, strlen(logMessage), temp))
       die("fwrite Failed");
     fclose(temp);
    }
  }
}

int main(int argc, char * argv[])
{
  if(strlen(argv[2]) > 48 || strcmp(argv[2], "server")==0)
  {
    die("Your name cannot be 'server' and must be 48 characters or less with no spaces\n");
  }
  if(argc < 3|| argc > 6)
  {
    die(PROPERUSE);
  }
  char mode  = *(argv[1] + 1);
  if(mode == 'c')
  {
    client(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
  }
  else if(mode == 's')
  {
    server(atoi(argv[2]));
  }
  else
  {
    die(PROPERUSE);
  }
  return 1;
}


                                                                                                                                