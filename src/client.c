#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #define close closesocket
    #define sleep Sleep
#else
    #include <netdb.h>
    #include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "tinycthread.h"

#define QUEUE_SIZE 1048576
#define RECV_SIZE (2*32*32*32)

#define STR_(x) #x
#define STR(x) STR_(x)

static int client_enabled = 0;
static int running = 0;
static int sd = 0;
static int bytes_sent = 0;
static int bytes_received = 0;
static char buf[QUEUE_SIZE] = { 0 };
static int bpos = 0;
static thrd_t recv_thread;
static mtx_t mutex;

void client_enable() {
    client_enabled = 1;
}

void client_disable() {
    client_enabled = 0;
}

int get_client_enabled() {
    return client_enabled;
}

int client_sendall(int sd, char *data, int length) {
    if (!client_enabled) {
        return 0;
    }
    int count = 0;
    while (count < length) {
        int n = send(sd, data + count, length, 0);
        if (n == -1) {
            return -1;
        }
        count += n;
        length -= n;
        bytes_sent += n;
    }
    return 0;
}

void client_send(char *data) {
    if (!client_enabled) {
        return;
    }
    int size = strlen(data);
    uint32_t len = htonl(size);
    if (client_sendall(sd, (char *) &len, 4) == -1) {
        perror("client_sendall");
        exit(1);
    }
    if (client_sendall(sd, data, size) == -1) {
        perror("client_sendall");
        exit(1);
    }
}

void client_version(int version) {
    if (!client_enabled) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "V,%d", version);
    client_send(buffer);
}

void client_login(const char *username, const char *identity_token) {
    if (!client_enabled) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "A,%s,%s", username, identity_token);
    client_send(buffer);
}

void client_position(float x, float y, float z, float rx, float ry) {
    if (!client_enabled) {
        return;
    }
    static float px, py, pz, prx, pry = 0;
    float distance =
        (px - x) * (px - x) +
        (py - y) * (py - y) +
        (pz - z) * (pz - z) +
        (prx - rx) * (prx - rx) +
        (pry - ry) * (pry - ry);
    if (distance < 0.0001) {
        return;
    }
    px = x; py = y; pz = z; prx = rx; pry = ry;
    char buffer[1024];
    snprintf(buffer, 1024, "P,%.2f,%.2f,%.2f,%.2f,%.2f", x, y, z, rx, ry);
    client_send(buffer);
}

void client_chunk(int p, int q, int r) {
    if (!client_enabled) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "C,%d,%d,%d", p, q, r);
    client_send(buffer);
}

void client_block(int x, int y, int z, int w) {
    if (!client_enabled) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "B,%d,%d,%d,%d", x, y, z, w);
    client_send(buffer);
}

void client_light(int x, int y, int z, int w) {
    if (!client_enabled) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "L,%d,%d,%d,%d", x, y, z, w);
    client_send(buffer);
}

void client_talk(const char *text) {
    if (!client_enabled) {
        return;
    }
    if (strlen(text) == 0) {
        return;
    }
    char buffer[1024];
    snprintf(buffer, 1024, "T,%s", text);
    client_send(buffer);
}

char *client_recv(size_t *size) {
    if (!client_enabled) {
        return 0;
    }
    char *result = 0;
    mtx_lock(&mutex);
    if (bpos > 0) {
        result = malloc(bpos);
        memcpy(result, buf, bpos);
        bytes_received += bpos;
        *size = bpos;
        bpos = 0;
    }
    mtx_unlock(&mutex);
    return result;
}

int recv_worker(void *arg) {
    char *data = malloc(RECV_SIZE + 1);
    uint32_t size;
    while (1) {
        if (recv(sd, &size, 4, 0) <= 0) {
            if (running) {
                perror("recv at " __FILE__ ":" STR(__LINE__));
                exit(1);
            }
            else {
                break;
            }
        }
        size = ntohl(size);
        if (size > RECV_SIZE) {
            fprintf(stderr, "Message too big\n");
            exit(1);
        }
        int t = 0;
        while (t < size) {
            int len = 0;
            if ((len = recv(sd, data+t, size-t, 0)) <= 0) {
                if (running) {
                    perror("recv at " __FILE__ ":" STR(__LINE__));
                    exit(1);
                } else {
                    break;
                }
            }
            t += len;
        }
        data[size++] = '\0';
        while (1) {
            int done = 0;
            mtx_lock(&mutex);
            if (bpos + size + sizeof(size_t) < QUEUE_SIZE) {
                *(size_t *)&buf[bpos] = size;
                memcpy(buf + bpos + sizeof(size_t), data, size);
                bpos += size + sizeof(size_t);
                done = 1;
            }
            mtx_unlock(&mutex);
            if (done) {
                break;
            }
            sleep(0);
        }
    }
    free(data);
    return 0;
}

void client_connect(char *hostname, int port) {
    if (!client_enabled) {
        return;
    }
    struct hostent *host;
    struct sockaddr_in address;
    if ((host = gethostbyname(hostname)) == 0) {
        perror("gethostbyname");
        exit(1);
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ((struct in_addr *)(host->h_addr_list[0]))->s_addr;
    address.sin_port = htons(port);
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    if (connect(sd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("connect");
        exit(1);
    }
}

void client_start() {
    if (!client_enabled) {
        return;
    }
    running = 1;
    bpos = 0;
    mtx_init(&mutex, mtx_plain);
    if (thrd_create(&recv_thread, recv_worker, NULL) != thrd_success) {
        perror("thrd_create");
        exit(1);
    }
}

void client_stop() {
    if (!client_enabled) {
        return;
    }
    running = 0;
    close(sd);
    // if (thrd_join(recv_thread, NULL) != thrd_success) {
    //     perror("thrd_join");
    //     exit(1);
    // }
    // mtx_destroy(&mutex);
    bpos = 0;
    // printf("Bytes Sent: %d, Bytes Received: %d\n",
    //     bytes_sent, bytes_received);
}
