#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT         "9034"

/** Types **/
struct user_t;
struct room_t;

struct user_t {
    int  fd;

    char *name;
    struct room_t *room;
};

struct room_t {
    int top;
    int max;

    char *name;
    struct user_t **users;
};

/** Globals **/
enum {
    MAXDATASIZE  = 2048,

    NROOMS       = 5,
    ROOMUSERS    = 8,
    MAXUSERS     = NROOMS * MAXUSERS + 1, // +1 for listener

    SERVROOMI    = 0,                     // server room index
    USERROOMI    = 1                      // user room index
};

/* Commands */
enum {
    CMDWLCM,
    CMDNICK,
    CMDROOM,
    CMDLIST,
    CMDHELP,
    NCMD,
    NOTCMD
};

const char *commands[] = {
    "!welcome",
    "!nick",
    "!room",
    "!list",
    "!help"
};

static struct room_t rooms[NROOMS]; // rooms for users
static struct user_t *listener;     // server is an user too
static struct room_t *serv_room;    // room for handling all users

/** Functions **/
struct user_t *new_user(int fd)
{
    struct user_t *u = malloc(sizeof(*u));
    if (u == NULL)
        return NULL;

    u->fd = fd;
    u->name = strdup("anonymous");
    if (u->name == NULL) {
        free(u);
        return NULL;
    }
    u->room = NULL;

    return u;
}

int add_user(struct room_t *room, struct user_t *user)
{
    if (room->top >= room->max)
        return -1; // the room is full

    user->room = room;
    room->users[room->top++] = user;

    return room->top;
}

int remove_user(struct room_t *room, int fd)
{
    for (int i = 0; i < room->top; ++i)
        if (room->users[i]->fd == fd) {
            room->users[i]->room = NULL;
            room->users[i] = room->users[--room->top];

            return room->top;
        }

    return -1; // that room has no fd in it
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int accept_con(int fd)
{
    int newfd;
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;
    char remoteIP[INET6_ADDRSTRLEN];

    addrlen = sizeof(remoteaddr);
    newfd   = accept(fd,
                     (struct sockaddr *) &remoteaddr,
                     &addrlen);

    if (newfd == -1)
        perror("accept");
    else
        printf("selectserver: new connection from %s on "
           "socket %d\n",
           inet_ntop(remoteaddr.ss_family,
                     get_in_addr((struct sockaddr *) &remoteaddr),
                     remoteIP, INET6_ADDRSTRLEN),
           newfd);

    return newfd;
}

int close_con(struct user_t *user, int n)
{
    if (n == 0)
        printf("chatserver: socket %d hung up\n", user->fd);
    else
        perror("recv");

    if (user->room != serv_room) // if user was in a custom room
        remove_user(user->room, user->fd);

    remove_user(serv_room, user->fd);
    close(user->fd);
    free(user);
    FD_CLR(user->fd, &master); // remove from master set

    return 0;
}

// returns prepared for accept() server socket
int get_serv_socket(void)
{
    int sockfd;
    int rv;
    int yes = 1; // for setsockopt() SO_REUSEADDR
    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(sockfd);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai);

    if (listen(sockfd, MAXUSERS) == -1) {
        perror("listen");
        exit(3);
    }

    return sockfd;
}

/* Commands */
int command_welcome(struct user_t *user)
{
    char resp[MAXDATASIZE];
    strcpy(resp,
           "Welcome to oss chat!\n"
           "There are %d available rooms.\n"
           "To see list of available command type '!help'.\n"
           "To see this message again type '!welcome'.\n");


    send(user->fd, resp, strlen(resp), 0);

    return 0;
}

int command_nick(struct user_t *user, const char *newnick)
{
    char resp[MAXDATASIZE];
    if (strlen(newnick) > strlen(user->name)) {
        char *buf = realloc(user->name, strlen(newnick) + 1);
        if (buf == NULL)
            return 1;
        strcpy(buf, newnick);
        user->name = buf;
    } else
        strcpy(user->name, newnick);

    strcpy(resp, "Nickname was successfully changed.\n");
    send(user->fd, resp, strlen(resp), 0);

    return 0;
}

int command_room(struct user_t *user, const char *newroom)
{

}

int command_help(struct user_t *user)
{
    char resp[MAXDATASIZE];
    strcpy(resp, "The list of available commands:\n");
    for (int i = 0; i < NCMD; ++i) {
        switch (i) {
        case CMDWLCM:
            sprintf(resp, "%s'%s' - prints server's welocme message.\n",
                    resp, commands[CMDWLCM]);
            break;

        case CMDNICK:
            sprintf(resp, "%s'%s newnick' - sets 'newnick' to user.\n",
                    resp, commands[CMDNICK]);
            break;

        case CMDHELP:
            sprintf(resp, "%s'%s' - prints list of available commands and their syntax.\n",
                    resp, commands[CMDHELP]);
            break;

        default: break;
        }
    }

    send(user->fd, resp, strlen(resp), 0);

    return 0;
}

int handle_message(struct user_t *user, const char *msg)
{
    if (strlen(msg) <= 0) // is empty
        return -1;

    int  i, rv; // rv for checking result of scanf
    int  cmd = NOTCMD;

    char next_op[MAXDATASIZE];
    char *p, *err_msg;

    if (msg[0] == '!') { // is message a command
        rv = sscanf(msg, "%100s", next_op);
        p = msg + strlen(next_op); // get position after command

        for (i = 0; i < NCMD; ++i)
            if (strcmp(next_op, commands[i]) == 0) {
                cmd = i;
                break;
            }
    }

    if (user->room != serv_room) {
        switch (cmd) {
        case NOTCMD:
            for(i = 0; i < user->room->top; ++i)
                if (send(user->room->users[i]->fd, msg, strlen(msg), 0) == -1)
                    perror("send");

            break;

        case CMDWLCM:
            command_welcome(user);
            break;

        case CMDNICK:
            rv = sscanf(p, "%100s", next_op);
            if (rv < 1 || rv == EOF) {
                err_msg = "Missed argument: 'newnick'.\n";
                send(user->fd, err_msg, strlen(err_msg), 0);
            } else {
                command_nick(user, next_op);
            }

            break;

        case CMDHELP:
            command_help(user);
            break;

        default: break;
        }
    }

    return cmd;
}

void init_server(void)
{

}

void main_loop(void)
{


    // keep track of the static fd_set master;               // master file descriptor setbiggest file descriptor


    int i, j, rv;
    struct user_t *p;
    for (;;) {


        // run through the existing connections looking for data to read
        for(i = 0; i < serv_room.top; ++i) {
            p = serv_room.users[i];

            if (FD_ISSET(p->fd, &read_fds)) {
                if (p->fd == listener->fd) {
                    // handle new connections
                    newfd = accept_con(p->fd);

                    if (newfd > -1) {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }

                        add_user(&serv_room, new_user(newfd));
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(p->fd, buf, sizeof(buf), 0)) <= 0)
                        close_con(p, nbytes);
                    else
                        handle_message(p, buf);
                }
            }
        }
    }
}

void end_server(void)
{int
    for (int i = 1; i < serv_room.top; ++i) {
        close(serv_room.users[i]->fd);
        free(serv_room.users[i]);
    }
}

int main(void)
{
    /* Variables */
    fd_set master;    // master file descriptor set
    fd_set read_fds;  // temp file descriptor list for select()
    int    fdmax;     // maximum file descriptor number
    int    newfd;     // newly accept()ed socket descriptor

    char msg[MAXDATASIZE]; // buffer for server data
    char buf[MAXDATASIZE]; // buffer for client data
    int  nbytes;           // count of sent bytes

    int i, j;
    struct user_t *p;

    /* Initialization */
    serv_room->top = 0;
    serv_room->max = MAXUSERS;
    serv_room->users = malloc(sizeof(*serv_room->users) * serv_room->max);
    assert(serv_room->users != NULL);

    listener = new_user(get_serv_socket());
    assert(listener != NULL);
    listener->name = strdup("server");
    assert(listener->name != NULL);
    add_user(serv_room, listener);

    for (i = USERROOMI; i < NROOMS; ++i) { // init rooms
        rooms[i].max    = ROOMUSERS;
        rooms[i].top    = 0;
        rooms[i].users  = malloc(sizeof(*rooms[i].users) * rooms[i].max);
        assert(rooms[i].users != NULL);
    }

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(listener->fd, &master);

    fdmax = listener->fd;

    /* Main loop */
    for (;;) {
        read_fds = master; // copying because select changes sets
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for (i = 0; i < serv_room->top; ++i)
    }


    main_loop();
    end_server();
    return 0;
}
