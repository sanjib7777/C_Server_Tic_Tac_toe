#ifndef WSSERVER_H
#define WSSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int ws_create_server(int port) {
    int sockfd;
    struct sockaddr_in servaddr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        return -1;

    if (listen(sockfd, MAX_CLIENTS) < 0)
        return -1;

    return sockfd;
}

int ws_accept_client(int server_fd) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    return accept(server_fd, (struct sockaddr *)&addr, &addrlen);
}

void base64_encode(const uint8_t *in, size_t inlen, char *out) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (size_t i = 0, j = 0; i < inlen;) {
        uint32_t octet_a = i < inlen ? in[i++] : 0;
        uint32_t octet_b = i < inlen ? in[i++] : 0;
        uint32_t octet_c = i < inlen ? in[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        out[j++] = table[(triple >> 6) & 0x3F];
        out[j++] = table[triple & 0x3F];
    }

    size_t mod = inlen % 3;
    if (mod) out[4 * ((inlen + 2) / 3) - 1] = '=';
    if (mod == 1) out[4 * ((inlen + 2) / 3) - 2] = '=';
    out[4 * ((inlen + 2) / 3)] = 0;
}

int ws_handshake(int client_fd) {
    char buffer[2048];
    int len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (len <= 0) return 0;
    buffer[len] = 0;

    const char *key_hdr = "Sec-WebSocket-Key: ";
    char *key_start = strstr(buffer, key_hdr);
    if (!key_start) return 0;
    key_start += strlen(key_hdr);
    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) return 0;

    char key[256];
    strncpy(key, key_start, key_end - key_start);
    key[key_end - key_start] = 0;

    char concat[512];
    snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);

    uint8_t hash[SHA_DIGEST_LENGTH];
    SHA1((uint8_t *)concat, strlen(concat), hash);

    char encoded[128];
    base64_encode(hash, SHA_DIGEST_LENGTH, encoded);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n", encoded);

    send(client_fd, response, strlen(response), 0);
    return 1;
}

int ws_recv(int client_fd, char *buffer, int buflen) {
    uint8_t header[2];
    if (recv(client_fd, header, 2, 0) <= 0) return -1;

    int payload_len = header[1] & 0x7F;
    if (payload_len == 126) {
        uint8_t extended[2];
        recv(client_fd, extended, 2, 0);
        payload_len = (extended[0] << 8) | extended[1];
    } else if (payload_len == 127) {
        uint8_t extended[8];
        recv(client_fd, extended, 8, 0);
        payload_len = 0; // For simplicity, ignore large frames
    }

    uint8_t mask[4];
    recv(client_fd, mask, 4, 0);

    char *msg = malloc(payload_len);
    if (!msg) return -1;
    recv(client_fd, msg, payload_len, 0);

    for (int i = 0; i < payload_len; i++) {
        buffer[i] = msg[i] ^ mask[i % 4];
    }

    free(msg);
    return payload_len;
}

int ws_send(int client_fd, const char *msg, int msg_len) {
    uint8_t header[10];
    int header_len = 0;

    header[0] = 0x81; // FIN + text frame

    if (msg_len <= 125) {
        header[1] = msg_len;
        header_len = 2;
    } else if (msg_len <= 65535) {
        header[1] = 126;
        header[2] = (msg_len >> 8) & 0xFF;
        header[3] = msg_len & 0xFF;
        header_len = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++) header[2 + i] = 0;
        header[9] = msg_len;
        header_len = 10;
    }

    if (send(client_fd, header, header_len, 0) < 0) return -1;
    if (send(client_fd, msg, msg_len, 0) < 0) return -1;

    return msg_len;
}

#endif // WSSERVER_H
