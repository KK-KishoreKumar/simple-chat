# Simple chatserver
by oss

## Usage
To start server just type:

`./server`

To connect to the server I use telnet as client:

`telnet <hostname> 9034`

#### Available Commands:
- **!welcome** - prints welcome message.
- **!list** - prints list of available rooms.
- **!help** - prints help message.
- **!nick &lt;newnick&gt;** - sets &lt;newnick&gt; to user.
- **!room &lt;newroom&gt;** - changes user's room to &lt;newroom&gt;


## Compilation
I compiled it on 64-bit linux using following command:

`gcc server.c -Wall -Wextra -Wconversion -pedantic -std=gnu11 -o server`
