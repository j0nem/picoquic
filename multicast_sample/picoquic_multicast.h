/*
 * Author: Christian Huitema
 * Copyright (c) 2020, Private Octopus, Inc.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef PICOQUIC_MULTICAST_H
#define PICOQUIC_MULTICAST_H
/* Header file for the picoquic multicast project.
* It contains the definitions common to client and server */

#include "picoquic_packet_loop.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The demo program currently uses fixed example values below
#define PICOQUIC_MULTICAST_ALPN "picoquic_multicast_test"
#define PICOQUIC_MULTICAST_SNI "localhost"
#define PICOQUIC_MULTICAST_GROUP_IP "232.10.1.12"
#define PICOQUIC_MULTICAST_GROUP_PORT 1234

#define PICOQUIC_MULTICAST_MAX_CLIENTS 8

#define PICOQUIC_MULTICAST_NO_ERROR 0
#define PICOQUIC_MULTICAST_INTERNAL_ERROR 0x101
#define PICOQUIC_MULTICAST_NAME_TOO_LONG_ERROR 0x102
#define PICOQUIC_MULTICAST_NO_SUCH_FILE_ERROR 0x103
#define PICOQUIC_MULTICAST_FILE_READ_ERROR 0x104
#define PICOQUIC_MULTICAST_FILE_CANCEL_ERROR 0x105

#define PICOQUIC_MULTICAST_CLIENT_DATA_FILENAME "output.txt"
#define PICOQUIC_MULTICAST_CLIENT_TICKET_STORE "multicast_ticket_store.bin"
#define PICOQUIC_MULTICAST_CLIENT_TOKEN_STORE "multicast_token_store.bin"
#define PICOQUIC_MULTICAST_CLIENT_QLOG_DIR "./log"
#define PICOQUIC_MULTICAST_SERVER_QLOG_DIR "./log"

#define PICOQUIC_MULTICAST_SENDER_MAX_FILES 32

typedef struct st_multicast_sender_datagram_ctx_t {
    struct st_multicast_sender_datagram_ctx_t* next_datagram;
    struct st_multicast_sender_datagram_ctx_t* previous_datagram;
    size_t name_length;
    size_t file_length;
    size_t file_sent;
    FILE* F;
    unsigned int is_name_sent : 1;
    unsigned int is_file_open : 1;
    unsigned int is_datagram_finished : 1;
} multicast_sender_datagram_ctx_t;

typedef struct st_multicast_sender_ctx_t {
    picoquic_quic_t* quic;
    picoquic_multicast_channel_t *mc_channel;
    char const* file_path;
    multicast_sender_datagram_ctx_t* first_datagram;
    multicast_sender_datagram_ctx_t* last_datagram;
    int is_disconnected;
} multicast_sender_ctx_t;

int picoquic_multicast_client(char const *server_name, int server_port, char const *default_dir);

int picoquic_multicast_sender_start(int server_port, 
    const char* server_cert, const char* server_key,
    char const* file_path, 
    picoquic_multicast_channel_t* channel, picoquic_network_thread_ctx_t** thread_ctx,
    multicast_sender_ctx_t* sender_ctx);
    
void picoquic_multicast_sender_stop(picoquic_network_thread_ctx_t* thread_ctx, multicast_sender_ctx_t* sender_ctx);

int picoquic_multicast_server(int server_port, int sender_port, const char *server_cert, const char *server_key, const char *served_file);

#ifdef __cplusplus
}
#endif

#endif