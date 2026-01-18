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

#define PICOQUIC_MULTICAST_DATAGRAM_HEADER_LENGTH 9

#define PICOQUIC_MULTICAST_CLIENT_DATA_FILENAME "output"
#define PICOQUIC_MULTICAST_CLIENT_TICKET_STORE "multicast_ticket_store.bin"
#define PICOQUIC_MULTICAST_CLIENT_TOKEN_STORE "multicast_token_store.bin"
#define PICOQUIC_MULTICAST_CLIENT_QLOG_DIR "./log"
#define PICOQUIC_MULTICAST_SERVER_QLOG_DIR "./log"

#define PICOQUIC_MULTICAST_SENDER_MAX_FILES 32

typedef struct st_multicast_server_ctx_t multicast_server_ctx_t;

typedef struct st_multicast_sender_ctx_t {
    FILE* F;
    size_t file_length;
    size_t file_sent;
    unsigned int file_was_opened : 1;
    uint64_t current_fragment_number;
    picoquic_network_thread_ctx_t* thread_ctx;
    multicast_server_ctx_t* server_ctx;
} multicast_sender_ctx_t;

typedef struct st_multicast_server_ctx_t
{
    char const *served_filename;
    picoquic_multicast_channel_t *mc_channel;
    int nb_joined_clients;
    int nb_active_clients;
    multicast_sender_ctx_t sender_app_ctx;
    picoquic_packet_loop_param_t* sender_loop_params;
    int* sender_thread_ret;
    int sender_running;
    int client_threshold;
    int is_local;
    const char *server_cert; 
    const char *server_key;
    int sender_port;
    picoquic_quic_t* quic;
    int is_closing_sender;
    int is_closing_server;
} multicast_server_ctx_t;

int multicast_client(char const *server_name, int server_port, 
    char const *default_dir, int max_rate);

int multicast_sender_start(multicast_sender_ctx_t* sender_ctx, int* thread_ret, picoquic_packet_loop_param_t* param);

int multicast_server(int server_port, int sender_port, const char *server_cert, 
    const char *server_key, int max_rate, const char *served_file, int client_threshold, int is_local);

#ifdef __cplusplus
}
#endif

#endif