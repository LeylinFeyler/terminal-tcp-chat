# Terminal TCP Chat (C)

This is a simple multi-client TCP chat written in C.

The server accepts multiple clients using `poll`.
Each message is formatted on the server with a timestamp and username, then sent to all connected clients.
All messages are stored in `chat.log`.

When a new client connects, the server sends the full chat history.

---

## Build

Compile the server and client:

```
gcc server.c helpers.c -o server
gcc client.c helpers.c -o client
```

---

## Run

Start the server:

```
./server
```

Then open one or more terminals and run:

```
./client
```

Enter your name and start typing messages.

