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

#ifndef PICOQUIC_DGRAMSPL_H
#define PICOQUIC_DGRAMSPL_H
/* Header file for the picoquic dgramspl project. 
 * It contains the definitions common to client and server */

#ifdef __cplusplus
extern "C" {
#endif

#define PICOQUIC_DGRAMSPL_ALPN "picoquic_dgramspl"
#define PICOQUIC_DGRAMSPL_SNI "test.example.com"

#define PICOQUIC_DGRAMSPL_NO_ERROR 0
#define PICOQUIC_DGRAMSPL_INTERNAL_ERROR 0x101
#define PICOQUIC_DGRAMSPL_NAME_TOO_LONG_ERROR 0x102
#define PICOQUIC_DGRAMSPL_NO_SUCH_FILE_ERROR 0x103
#define PICOQUIC_DGRAMSPL_FILE_READ_ERROR 0x104
#define PICOQUIC_DGRAMSPL_FILE_CANCEL_ERROR 0x105

#define PICOQUIC_DGRAMSPL_DATAGRAM_HEADER_LENGTH 9

#define PICOQUIC_DGRAMSPL_CLIENT_DATA_FILENAME "output"

#define PICOQUIC_DGRAMSPL_CLIENT_TICKET_STORE "dgramspl_ticket_store.bin";
#define PICOQUIC_DGRAMSPL_CLIENT_TOKEN_STORE "dgramspl_token_store.bin";
#define PICOQUIC_DGRAMSPL_CLIENT_QLOG_DIR "./log";
#define PICOQUIC_DGRAMSPL_SERVER_QLOG_DIR "./log";

#define PICOQUIC_DGRAMSPL_BACKGROUND_MAX_FILES 32

int dgramspl_client(char const* server_name, int server_port, char const* default_dir);

int dgramspl_server(int server_port, const char* pem_cert, const char* pem_key, char * file_path);

#ifdef __cplusplus
}
#endif

#endif
