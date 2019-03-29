Jackson Welles
jw3350

How to run
type 'make'
type './UdpChat'

command line arguments are the same as in the project description

if this doesn't work:
type 'gcc UdpChat.c'
type './a.out'

command line arguments are the same as in the project description

Commands:
send <user> <message> used to send a message
ex. send joe hi
reg used to register
dereg used to deregister

UdpChat overview:
There are three major aspects to this program

1. Forking to deal with sent packets
The program forks to create two children, one that listens for packets and one that listens for user input.
The packet child listens on the specified port and uses a pipe to send pacekts up to the parent process.
The user input child is also uses a pipe and is a child of the packet-listening child process. The parent
SIGUSR2 to kill the childen.

2. The client table
This is just the table with the name, ips, ports, and statuses of users. It works identically to the table 
described in the assignment

3. The "killQueue"
This is what i came up with to deal with timeouts, it is a queue that stores entries for messages that require
ACKs, after the main parent has acted on any packets or user messages, it will go through the queue and see
if any messages need to be resent or if action needs to be taken for a timed out connection. The implementation is
a little clunky as I was running low on time but it works.

Quirks/features:
The table uses names as unique identifiers, if you try registering with the same name of another user the program
will exit and ask you to try a different one

Because of the way the packet system works your name cannot be "server"

There is a maximum message size of 750 characters, if you exceed this the progam will only send the first 750
characters. The maximum message sized can be changed by modifying the BUFFSIZE macro

There is no Negative Ack packet from the server if it doesn't like your registration request. It'll just ingore
the message and the client will be prompted that the name they want might be taken.

If you're waiting on an ACK from somebody the program won't let you send them any more messages until they reply
or time out

you can message yourself

Types of Packets:
REG and DRG: sent by clients to server to reg and dereg
ACK: used by clients to ACK sent messages and server to ACK deregs and stored offline messages
OFL: used by the server to indicate an offline message
MSG: A message packet can be sent to either the server or the client and will have the exact same
structure for both. The client will print the message and send an ACK, the server will save it and
send a special ACK that is flagged to be offline
UPD: An update packet from the server telling the client to update its tables.