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

/* The "dgramspl" project builds a simple file transfer program that can be
 * instantiated in client or server mode. The "dgramspl_server" implements
 * the server components of the dgramspl application. 
 *
 * Developing the server requires two main components:
 *  - the server "callback" that implements the server side of the
 *    application protocol, managing a server side application context
 *    for each connection.
 *  - the server loop, that reads messages on the socket, submits them
 *    to the Quic context, let the server prepare messages, and send
 *    them on the appropriate socket.
 *
 * The Sample Server uses the "qlog" option to produce Quic Logs as defined
 * in https://datatracker.ietf.org/doc/draft-marx-qlog-event-definitions-quic-h3/.
 * This is an optional feature, which requires linking with the "loglib" library,
 * and using the picoquic_set_qlog() API defined in "autoqlog.h". . When a connection
 * completes, the code saves the log as a file named after the Initial Connection
 * ID (in hexa), with the suffix ".server.qlog".
 */

#include <stdint.h>
#include <stdio.h>
#include <picoquic.h>
#include <picosocks.h>
#include <picoquic_utils.h>
#include <autoqlog.h>
#include "dgramspl.h"
#include "picoquic_packet_loop.h"

/* Server context and callback management:
 *
 * The server side application context is created for each new connection,
 * and is freed when the connection is closed. It contains a list of
 * server side stream contexts, one for each stream open on the
 * connection. Each stream context includes:
 *  - description of the stream state:
 *      name_read or not, FILE open or not, stream reset or not,
 *      stream finished or not.
 *  - the number of file name bytes already read.
 *  - the name of the file requested by the client.
 *  - the FILE pointer for reading the data.
 * Server side stream context is created when the client starts the
 * stream. It is closed when the file transmission
 * is finished, or when the stream is abandoned.
 *
 * The server side callback is a large switch statement, with one entry
 * for each of the call back events.
 */

typedef struct st_dgramspl_server_ctx_t {
    char file_path[1024];
    size_t file_path_len;
    size_t file_length;
    unsigned int file_was_opened : 1;
    unsigned int file_error : 1;
    uint64_t current_fragment_number;
    FILE* F;
    size_t file_sent;
} dgramspl_server_ctx_t;

int dgramspl_server_open_file(dgramspl_server_ctx_t* server_ctx)
{
    int ret = 0;

    /* Use the picoquic_file_open API for portability to Windows and Linux */
    server_ctx->F = picoquic_file_open(server_ctx->file_path, "rb");

    if (server_ctx->F == NULL) {
        ret = PICOQUIC_DGRAMSPL_NO_SUCH_FILE_ERROR;
    } else {
        /* Assess the file size, as this is useful for data planning */
        long sz;
        fseek(server_ctx->F, 0, SEEK_END);
        sz = ftell(server_ctx->F);

        if (sz <= 0) {
            server_ctx->F = picoquic_file_close(server_ctx->F);
            ret = PICOQUIC_DGRAMSPL_FILE_READ_ERROR;
        }
        else {
            server_ctx->file_length = (size_t)sz;
            fseek(server_ctx->F, 0, SEEK_SET);
            ret = 0;
        }
    }

    return ret;
}

void dgramspl_server_delete_context(dgramspl_server_ctx_t* server_ctx)
{
    /* release the memory */
    free(server_ctx);
}

static int dgramspl_server_mark_datagram_ready(picoquic_cnx_t* cnx, int is_ready)
{
    /* Mark the connection as active for datagram transmission. 
    * The callback will be asked to provide data when the connection is ready. */
    int ret = picoquic_mark_datagram_ready(cnx, is_ready);
    if (ret != 0) {
        fprintf(stdout, "Error %d, cannot mark datagram ready\n", ret);
    }

    return ret;
}

int dgramspl_server_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    dgramspl_server_ctx_t* server_ctx = (dgramspl_server_ctx_t*)callback_ctx;

    /* If this is the first reference to the connection, the application context is set
     * to the default value defined for the server. This default value contains the pointer
     * to the file directory in which all files are defined.
     */
    if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        server_ctx = (dgramspl_server_ctx_t *)malloc(sizeof(dgramspl_server_ctx_t));
        if (server_ctx == NULL) {
            /* cannot handle the connection */
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        else {
            dgramspl_server_ctx_t* d_ctx = (dgramspl_server_ctx_t*)picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx));
            if (d_ctx != NULL) {
                memcpy(server_ctx, d_ctx, sizeof(dgramspl_server_ctx_t));
            }
            else {
                /* This really is an error case: the default connection context should never be NULL */
                memset(server_ctx, 0, sizeof(dgramspl_server_ctx_t));
                memset(server_ctx->file_path, 0, sizeof(server_ctx->file_path));
            }
            picoquic_set_callback(cnx, dgramspl_server_callback, server_ctx);
        }
    }

    if (ret == 0) {
        switch (fin_or_event) {
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
            break;
        case picoquic_callback_prepare_datagram:
            if (server_ctx->file_sent >= server_ctx->file_length && server_ctx->file_was_opened) {
                // File sending finished, stop sender loop and delete 
                // thread at this point and notify server via callback
                dgramspl_server_mark_datagram_ready(cnx, 0);
                break;
            }

            if (!server_ctx->file_was_opened && server_ctx->F == NULL) {
                ret = dgramspl_server_open_file(server_ctx);
                if (ret != 0) {
                    fprintf(stderr, "Error while opening the requested file: %i\n", ret);
                    break;
                }
            }

            // Use header to indicate fragment number and first/last fragment
            int is_first = 0;
            int is_last = 0;
            int header_length = PICOQUIC_DGRAMSPL_DATAGRAM_HEADER_LENGTH;

            /* Implement the zero copy callback */
            size_t available = server_ctx->file_length - server_ctx->file_sent + header_length;
            uint8_t *buffer;

            if (server_ctx->file_sent == 0 && server_ctx->current_fragment_number == 0) {
                is_first = 1;
            } else {
                server_ctx->current_fragment_number++;
            }
            
            if (available > length) {
                available = length;
            } else {
                is_last = 1;
            }

            if (available <= PICOQUIC_DGRAMSPL_DATAGRAM_HEADER_LENGTH) {
                // Provided buffer is too small, request larger buffer from picoquic
                picoquic_provide_datagram_buffer_ex(bytes, 0, picoquic_datagram_active_any_path);
            } else {
                buffer = picoquic_provide_datagram_buffer_ex(bytes, available, picoquic_datagram_active_any_path);

                if (buffer != NULL) {
                    uint8_t* start_fread = buffer;

                    uint8_t first_byte = 0;
                    first_byte = first_byte | (uint8_t) is_last << (uint8_t) 0;
                    first_byte = first_byte | (uint8_t) is_first << (uint8_t) 1;

                    memcpy(buffer, &first_byte, 1);
                    memcpy(buffer + 1, &(server_ctx->current_fragment_number), PICOQUIC_DGRAMSPL_DATAGRAM_HEADER_LENGTH - 1);
                    start_fread += header_length;
                    available -= header_length;

                    if (is_last == 1 && is_first != 1) {
                        fprintf(stdout, "is_last sent: First byte = 0x%x\n", buffer[0]);
                    } else if (is_last != 1 && is_first == 1) {
                        fprintf(stdout, "is_first sent: First byte = 0x%x\n", buffer[0]);
                    } else if (is_last == 1 && is_first == 1) {
                        fprintf(stdout, "is_first and is_last sent: First byte = 0x%x\n", buffer[0]);
                    }

                    size_t nb_read = fread(start_fread, 1, available, server_ctx->F);

                    if (nb_read != available) {
                        /* Error while reading the file */
                        ret = -1;           
                    }
                    else {
                        server_ctx->file_sent += nb_read;
                    }
                }
                else {
                    /* Should never happen according to callback spec. */
                    ret = -1;
                }
            }
            break;
        case picoquic_callback_prepare_to_send:
            /* Prepare to send STREAM data */
            break;
        case picoquic_callback_stream_reset: /* Client reset stream #x */
        case picoquic_callback_stop_sending: /* Client asks server to reset stream #x */
            break;
        case picoquic_callback_stateless_reset: /* Received an error message */
        case picoquic_callback_close: /* Received connection close */
        case picoquic_callback_application_close: /* Received application close */
            /* Delete the server application context */
            dgramspl_server_delete_context(server_ctx);
            picoquic_set_callback(cnx, NULL, NULL);
            break;
        case picoquic_callback_version_negotiation:
            /* The server should never receive a version negotiation response */
            break;
        case picoquic_callback_stream_gap:
            /* This callback is never used. */
            break;
        case picoquic_callback_almost_ready:
        case picoquic_callback_ready:
            /* New connection to a client is ready */
            if (server_ctx->file_was_opened == 0 && server_ctx->file_error == 0) {
                fprintf(stdout, "New connection to client is ready\n");

                int client_ret = dgramspl_server_open_file(server_ctx);

                if (client_ret == 0) {
                    /* If data needs to be sent, set the context as active */
                    fprintf(stdout, "Mark datagram ready\n");
                    ret = dgramspl_server_mark_datagram_ready(cnx, 1);
                    server_ctx->file_was_opened = 1;
                } else {
                    fprintf(stdout, "File open failed: 0x%x\n", client_ret);
                    /* If the file could not be read, mark the cnx as unready */
                    server_ctx->file_error = 1;
                    ret = dgramspl_server_mark_datagram_ready(cnx, 0);
                }
            }

            break;
        default:
            /* unexpected */
            break;
        }
    }

    return ret;
}

/* Server loop setup:
 * - Create the QUIC context.
 * - Open the sockets
 * - On a forever loop:
 *     - get the next wakeup time
 *     - wait for arrival of message on sockets until that time
 *     - if a message arrives, process it.
 *     - else, check whether there is something to send.
 *       if there is, send it.
 * - The loop breaks if the socket return an error. 
 */

int dgramspl_server(int server_port, const char* server_cert, const char* server_key, char* file_path)
{
    /* Start: start the QUIC process with cert and key files */
    int ret = 0;
    picoquic_quic_t* quic = NULL;
    char const* qlog_dir = PICOQUIC_DGRAMSPL_SERVER_QLOG_DIR;
    uint64_t current_time = 0;
    dgramspl_server_ctx_t default_context = { 0 };

    default_context.file_path_len = strlen(file_path);
    if (default_context.file_path_len > 1022) {
        ret = -1;
    } else {
        file_path[default_context.file_path_len] = 0;
        memcpy(default_context.file_path, file_path, default_context.file_path_len);

        printf("Starting Datagram Sample server on port %d\n", server_port);
        printf("Serving file %s\n", file_path);

        /* Create the QUIC context for the server */
        current_time = picoquic_current_time();
        /* Create QUIC context */
        quic = picoquic_create(8, server_cert, server_key, NULL, PICOQUIC_DGRAMSPL_ALPN,
            dgramspl_server_callback, &default_context, NULL, NULL, NULL, current_time, NULL, NULL, NULL, 0);

        if (quic == NULL) {
            fprintf(stderr, "Could not create server context\n");
            ret = -1;
        }
        else {
            picoquic_set_cookie_mode(quic, 2);

            picoquic_set_default_congestion_algorithm(quic, picoquic_bbr_algorithm);

            picoquic_set_qlog(quic, qlog_dir);

            picoquic_set_log_level(quic, 1);

            picoquic_set_key_log_file_from_env(quic);
        }
    }

    /* Wait for packets using the wait loop provided in the library.
     * On Linux, the default is to use UDP GSO when the system version is
     * recent enough to provide it, because this provides much better
     * performance. This may cause packet loss if a faulty driver fails
     * to provide UDP GSO and also does not return an error code when
     * doing so. In that case, the fourth zero below should be
     * changed to 1, i.e. passing "do_not_use_gso = 1". Or, better
     * still, get the faulty driver fixed.
     */
    if (ret == 0) {
        ret = picoquic_packet_loop(quic, server_port, 0, 0, 0, 0, NULL, NULL);
    }

    /* And finish. */
    printf("Server exit, ret = %d\n", ret);

    /* Clean up */
    if (quic != NULL) {
        picoquic_free(quic);
    }

    return ret;
}