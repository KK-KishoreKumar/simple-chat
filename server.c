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

#define PORT "9034"
#define MSGLEN(msg) (strlen(msg) + 1)

struct user_t;
struct room_t;

struct user_t *new_user(int fd);

int add_user(struct room_t *room, struct user_t *user);
int remove_user(struct room_t *room, int fd);

int  accept_con(int fd);
int  close_con(struct user_t *user, int n);
int  get_serv_socket(void);
void *get_in_addr(struct sockaddr *sa);

int send_to_room(struct user_t *user, const char *msg);
int send_msg(const struct user_t *receiver, const struct user_t *sender,
             const char *buf, int flags);

int command_welcome(const struct user_t *user);
int command_list(const struct user_t *user);
int command_help(const struct user_t *user);
int command_nick(struct user_t *user, const char *newnick);
int command_room(struct user_t *user, const char *newroom);
int handle_command(struct user_t *user, char *msg);
int handle_message(struct user_t *user, char *msg);


/** Types **/
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
    MAXDATASIZE  = 4096,

    NROOMS       = 4,
    ROOMUSERS    = 8,
    MAXUSERS     = NROOMS * ROOMUSERS + 1 // +1 for listener
};

const char *room_names[NROOMS] = {
    "General",
    "Holywars",
    "Games",
    "Questions"
};

static struct room_t rooms[NROOMS]; // rooms for users
static struct room_t serv_room;     // room for handle all users
static struct user_t *listener;     // server is an user too

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

int change_room(struct room_t *newroom, struct user_t *user)
{
    int rv = 0;
    struct room_t *tmp = (user->room == NULL) ? &serv_room : user->room;
    if (tmp != &serv_room)
        remove_user(user->room, user->fd);

    if ((rv = add_user(newroom, user)) == -1)
        add_user(tmp, user);

    return rv;
}

int accept_con(int fd)
{
    int newfd;
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;
    char remoteIP[INET6_ADDRSTRLEN];
    struct user_t *p;

    addrlen = sizeof(remoteaddr);
    newfd   = accept(fd,
                     (struct sockaddr *) &remoteaddr,
                     &addrlen);

    if (newfd == -1)
        perror("accept");
    else {
        printf("selectserver: new connection from %s on "
           "socket %d\n",
           inet_ntop(remoteaddr.ss_family,
                     get_in_addr((struct sockaddr *) &remoteaddr),
                     remoteIP, INET6_ADDRSTRLEN),
           newfd);

        p = new_user(newfd);
        add_user(&serv_room, p);
        command_welcome(p);
    }

    return newfd;
}

int close_con(struct user_t *user, int n)
{
    if (n == 0) {
        printf("chatserver: socket %d hung up\n", user->fd);
    } else {
        perror("recv");
        return 1;
    }

    if (user->room != &serv_room) // if user was in a custom room
        remove_user(user->room, user->fd);

    remove_user(&serv_room, user->fd);
    close(user->fd);
    free(user);

    return 0;
}

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

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

int send_to_room(struct user_t *user, const char *msg)
{
    for (int i = 0; i < user->room->top; ++i) {
        if (send_msg(user->room->users[i], user, msg, 0) == -1)
            perror("send");
    }

    return 0;
}

int send_msg(const struct user_t *receiver, const struct user_t *sender,
             const char *msg, int flags)
{
    int  rv;
    char resp[MAXDATASIZE];

    snprintf(resp, MAXDATASIZE, "%s: %s", sender->name, msg);

    rv = send(receiver->fd, resp, MSGLEN(resp), flags);

    return rv;
}

/* Commands functions */
int command_welcome(const struct user_t *user)
{
    char resp[MAXDATASIZE];
    snprintf(resp, MAXDATASIZE,
            "Welcome to oss chat!\n"
            "There are %d available rooms.\n"
            "To see list of available commands type '%s'.\n"
            "To see this message again type '%s'.\n",
            NROOMS, commands[CMDHELP], commands[CMDWLCM]);

    send_msg(user, listener, resp, 0);

    return 0;
}

int command_list(const struct user_t *user)
{
    char resp[MAXDATASIZE];
    snprintf(resp, MAXDATASIZE, "The list of available rooms:\n");
    for (int i = 0; i < NROOMS; ++i)
        snprintf(resp, MAXDATASIZE, "%sRoom %d - '%s' (%d/%d)\n",
                 resp, i+1, rooms[i].name, rooms[i].top, rooms[i].max);

    send_msg(user, listener, resp, 0);

    return 0;
}

int command_help(const struct user_t *user)
{
    char resp[MAXDATASIZE];
    strcpy(resp, "The list of available commands:\n");
    for (int i = 0; i < NCMD; ++i) {
        switch (i) {
        case CMDWLCM:
            snprintf(resp, MAXDATASIZE, "%s%s - prints server welcome message.\n",
                     resp, commands[i]);
            break;

        case CMDNICK:
            snprintf(resp, MAXDATASIZE, "%s%s <newnick> - sets <newnick> to user.\n",
                     resp, commands[i]);
            break;

        case CMDROOM:
            snprintf(resp, MAXDATASIZE, "%s%s <name_or_number> - enters user to "
                     "<name_or_number> room.\n", resp, commands[i]);
            break;

        case CMDLIST:
            snprintf(resp, MAXDATASIZE, "%s%s - prints list of available rooms.\n",
                     resp, commands[i]);
            break;

        case CMDHELP:
            snprintf(resp, MAXDATASIZE, "%s%s - prints this message.\n",
                     resp, commands[i]);
            break;

        default: break;
        }
    }

    send_msg(user, listener, resp, 0);

    return 0;
}

int command_nick(struct user_t *user, const char *newnick)
{
    char resp[MAXDATASIZE];
    if (strlen(newnick) > strlen(user->name)) {
        char *buf = realloc(user->name, strlen(newnick) + 1);
        if (buf == NULL)
            return 1; // not enough memory

        strcpy(buf, newnick);
        user->name = buf;
    } else
        strcpy(user->name, newnick);

    snprintf(resp, MAXDATASIZE, "Nickname was successfully changed.\n");
    send_msg(user, listener, resp, 0);

    return 0;
}

int command_room(struct user_t *user, const char *newroom)
{
    char resp[MAXDATASIZE];
    bool is_error  = false;
    int  roomi;

    for (roomi = 0; roomi < NROOMS; ++roomi) {
        if (strcmp(rooms[roomi].name, newroom) == 0)
            break;

        if (roomi == NROOMS-1) {
            is_error = true;
            snprintf(resp, MAXDATASIZE,"Incorrect name of room, type '%s' "
                     "to see list of available rooms.\n", commands[CMDLIST]);
        }
    }

    if (!is_error) {
        if (change_room(&rooms[roomi], user) == -1) {
            snprintf(resp, MAXDATASIZE, "Can't change room.\n");
        } else {
            snprintf(resp, MAXDATASIZE, "Welcome to room '%s'!\n",
                    rooms[roomi].name);
        }
    }

    send_msg(user, listener, resp, 0);

    return (is_error) ? 1 : 0;
}

int handle_command(struct user_t *user, char *msg)
{
    int  rv;
    char resp[MAXDATASIZE], next_op[100];

    int cmd = NOTCMD;
    const char *fmt = "%100s";
    char *p = (char *) msg;

    rv = sscanf(msg, fmt, next_op);
    p += strlen(next_op);

    for (int i = 0; i < NCMD; ++i)
        if (strcmp(next_op, commands[i]) == 0) {
            cmd = i;
            break;
        }

    switch (cmd) {
    case CMDWLCM:
        command_welcome(user);
        break;

    case CMDLIST:
        command_list(user);
        break;

    case CMDHELP:
        command_help(user);
        break;

    case CMDNICK:
        rv = sscanf(p, fmt, next_op);
        if (rv < 1 || rv == EOF) {
            snprintf(resp, MAXDATASIZE, "Incorrect syntax, type '%s' to see syntax of commands.\n",
                    commands[CMDHELP]);
            send_msg(user, listener, resp, 0);
        } else {
            command_nick(user, next_op);
        }

        break;

    case CMDROOM:
        rv = sscanf(p, fmt, next_op);
        if (rv < 1 || rv == EOF) {
            snprintf(resp, MAXDATASIZE, "Incorrect syntax, type '%s' to see syntax of commands.\n",
                    commands[CMDHELP]);
            send_msg(user, listener, resp, 0);
        } else {
            command_room(user, next_op);
        }

        break;

    default:
        break;
    }

    return cmd;
}

int handle_message(struct user_t *user, char *msg)
{
    char resp[MAXDATASIZE];
    int rv = NOTCMD;

    if (msg[0] == '!') // is message a command
        rv = handle_command(user, msg);

    if (rv == NOTCMD) {
        if (user->room != &serv_room) {
            send_to_room(user, msg);
        } else {
            snprintf(resp, MAXDATASIZE,
                    "You need to enter the room to send messages. "
                    "Input the '%s' command to see list of available rooms.\n",
                    commands[CMDLIST]);
            send_msg(user, listener, resp, 0);
        }
    }
    return rv;
}

int main(int argc, char *argv[])
{
    (void) argc; (void) argv;

    fd_set master;    // master file descriptor set
    fd_set read_fds;  // temp file descriptor list for select()
    int    fdmax;     // maximum file descriptor number
    int    newfd;     // newly accept()ed socket descriptor

    char buf[MAXDATASIZE]; // buffer for client data
    int  nbytes;           // count of sent bytes

    int i;
    struct user_t *p;


    /* Initialization */
    serv_room.top = 0;
    serv_room.max = MAXUSERS;
    serv_room.users = malloc(sizeof(*serv_room.users) * serv_room.max);
    assert(serv_room.users != NULL);

    listener = new_user(get_serv_socket());
    assert(listener != NULL);
    listener->name = strdup("server");
    assert(listener->name != NULL);
    add_user(&serv_room, listener);

    for (i = 0; i < NROOMS; ++i) { // init rooms
        rooms[i].max    = ROOMUSERS;
        rooms[i].top    = 0;
        rooms[i].name   = strdup(room_names[i]);
        assert(rooms[i].name != NULL);
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

        for (i = 0; i < serv_room.top; ++i) {
            p = serv_room.users[i];

            if (FD_ISSET(p->fd, &read_fds)) {
                if (p->fd == listener->fd) {
                    newfd = accept_con(listener->fd);
                    if (newfd > -1) {
                        FD_SET(newfd, &master);
                        if (newfd > fdmax)
                            fdmax = newfd;
                    }
                } else {
                    if ((nbytes = recv(p->fd, buf, MAXDATASIZE-1, 0)) <= 0) {
                        FD_CLR(p->fd, &master);
                        close_con(p, nbytes);
                    } else {
                        buf[nbytes] = '\0';
                        handle_message(p, buf);
                    }
                }
            }
        }
    }

    for (i = 0; i < NROOMS; ++i)
        free(rooms[i].name);

    for (i = 0; i < serv_room.top; ++i) {
        close(serv_room.users[i]->fd);
        free(serv_room.users[i]->name);
        free(serv_room.users[i]);
    }

    free(listener->name);
    free(listener);
    free(serv_room.name);

    return 0;
}
