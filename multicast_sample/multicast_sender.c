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

/* The "multicast" project builds a simple file transfer program that can be
 * instantiated in client or server mode. The "multicast_client" implements
 * the client components of the multicast application.
 *
 * Developing the client requires two main components:
 *  - the client "callback" that implements the client side of the
 *    application protocol, managing the client side application context
 *    for the connection.
 *  - the client loop, that reads messages on the socket, submits them
 *    to the Quic context, let the client prepare messages, and send
 *    them on the appropriate socket.
 *
 * The Multicast Client uses the "qlog" option to produce Quic Logs as defined
 * in https://datatracker.ietf.org/doc/draft-marx-qlog-event-definitions-quic-h3/.
 * This is an optional feature, which requires linking with the "loglib" library,
 * and using the picoquic_set_qlog() API defined in "autoqlog.h". When a connection
 * completes, the code saves the log as a file named after the Initial Connection
 * ID (in hexa), with the suffix ".client.qlog".
 */

#include <stdint.h>
#include <stdio.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <autoqlog.h>
#include <picoquic_packet_loop.h>
#include "picoquic_multicast.h"

 /* Background thread management:
  * 
  * The "background" multicast is an example of application split
  * between application threads for the application logic, and a
  * background thread for the picoquic stack. 
  * 
  * In the "background client" mode, the files are requested from
  * the UI thread, but the corresponding streams are created in the
  * background thread. The shared state between the two
  * threads is kept in the "background context", visible by
  * both threads, and all interactions with the stack happen
  * in the background thread, because the stack code is not
  * thread safe.
  * 
  * The goal of the multicast is not to demonstrate distributed programming
  * techniques. In a real application, the code running in the background
  * would probably send messages and events to the application, but that
  * kind of code is very application specific. In our multicast, we use a simple
  * ad hoc coordination between the two threads:
  * - the variable "nb_files" documents the number of files
  *   that have been documented by the client. It is updated by
  *   the UI thread when a new file is requested.
  * - the variable "nb_files_processed" documents the number
  *   of files for which a stream has been created. It is
  *   updated by the UI thread.
  * - the flag "is_closing_requested" is set by the application
  *   when it wants to close the connection. The background thread
  *   will call the `picoquic_close` API in the background thread,
  *   set the flag `is_closing` after that, and set the flag
  *   `is_disconnected` when the stack has completed the closing.
  * After the application updates the shared state, it calls the
  * API `picoquic_wake_up_network_thread`. This cause the background
  * thread to stop waiting for packets or timeout, and to issue
  * a loop callback `picoquic_packet_loop_wake_up`, during which the
  * application provided call can interact safely with the
  * picoquic stack.
  */

// static int multicast_sender_create_stream(picoquic_cnx_t* cnx,
//     multicast_sender_ctx_t* client_ctx, int file_rank)
// {
//     int ret = 0;
//     multicast_sender_stream_ctx_t* stream_ctx = (multicast_sender_stream_ctx_t*)
//         malloc(sizeof(multicast_sender_stream_ctx_t));

//     if (stream_ctx == NULL) {
//         fprintf(stdout, "Memory Error, cannot create stream for file number %d\n", (int)file_rank);
//         ret = -1;
//     }
//     else {
//         memset(stream_ctx, 0, sizeof(multicast_sender_stream_ctx_t));
//         if (client_ctx->first_stream == NULL) {
//             client_ctx->first_stream = stream_ctx;
//             client_ctx->last_stream = stream_ctx;
//         }
//         else {
//             client_ctx->last_stream->next_stream = stream_ctx;
//             client_ctx->last_stream = stream_ctx;
//         }
//         stream_ctx->file_rank = file_rank;
//         stream_ctx->stream_id = picoquic_get_next_local_stream_id(client_ctx->cnx, 0);
//         stream_ctx->name_length = strlen(client_ctx->file_names[file_rank]);

//         /* Mark the stream as active. The callback will be asked to provide data when 
//          * the connection is ready. */
//         ret = picoquic_mark_active_stream(cnx, stream_ctx->stream_id, 1, stream_ctx);
//         if (ret != 0) {
//             fprintf(stdout, "Error %d, cannot initialize stream for file number %d\n", ret, (int)file_rank);
//         }
//     }

//     return ret;
// }

static int multicast_sender_create_datagram(multicast_sender_ctx_t* sender_ctx)
{
    int ret = 0;
    multicast_sender_datagram_ctx_t* datagram_ctx = (multicast_sender_datagram_ctx_t*)
        malloc(sizeof(multicast_sender_datagram_ctx_t));

        
        if (datagram_ctx == NULL) {
        fprintf(stdout, "Memory Error, cannot create datagram for file\n");
        ret = -1;
    }
    else {
        memset(datagram_ctx, 0, sizeof(multicast_sender_datagram_ctx_t));
        if (sender_ctx->first_datagram == NULL) {
            sender_ctx->first_datagram = datagram_ctx;
            sender_ctx->last_datagram = datagram_ctx;
        }
        else {
            datagram_ctx->previous_datagram = sender_ctx->last_datagram;
            sender_ctx->last_datagram->next_datagram = datagram_ctx;
            sender_ctx->last_datagram = datagram_ctx;
        }
        datagram_ctx->name_length = strlen(sender_ctx->file_path);

        
        /* Mark the stream as active. The callback will be asked to provide data when 
        * the connection is ready. */
    //    ret = picoquic_mark_datagram_ready_multicast(sender_ctx->mc_channel, 1);
       fprintf(stdout, "debug: after picoquic_mark_datagram_ready_multicast\n");
        if (ret != 0) {
            fprintf(stdout, "Error %d, cannot mark datagram ready\n", ret);
        }
    }

    return ret;
}

static void multicast_sender_free_context(multicast_sender_ctx_t* sender_ctx)
{
    multicast_sender_datagram_ctx_t* datagram_ctx;

    while ((datagram_ctx = sender_ctx->first_datagram) != NULL) {
        sender_ctx->first_datagram = datagram_ctx->next_datagram;
        if (datagram_ctx->F != NULL) {
            (void)picoquic_file_close(datagram_ctx->F);
        }
        free(datagram_ctx);
    }
    sender_ctx->last_datagram = NULL;
}

void multicast_sender_delete_datagram_context(multicast_sender_ctx_t *sender_ctx, multicast_sender_datagram_ctx_t *datagram_ctx)
{
    /* Close the file if it was open */
    if (datagram_ctx->F != NULL) {
        datagram_ctx->F = picoquic_file_close(datagram_ctx->F);
    }
    /* Remove the context from the server's list */
    if (datagram_ctx->previous_datagram == NULL) {
        sender_ctx->first_datagram = datagram_ctx->next_datagram;
    }
    else {
        datagram_ctx->previous_datagram->next_datagram = datagram_ctx->next_datagram;
    }
    if (datagram_ctx->next_datagram == NULL) {
        sender_ctx->last_datagram = datagram_ctx->previous_datagram;
    }
    else {
        datagram_ctx->next_datagram->previous_datagram = datagram_ctx->previous_datagram;
    }

    /* release the memory */
    free(datagram_ctx);
}

int multicast_sender_callback(picoquic_multicast_channel_t* channel,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    multicast_sender_ctx_t* sender_ctx = (multicast_sender_ctx_t*)callback_ctx;
    multicast_sender_datagram_ctx_t* datagram_ctx = (multicast_sender_datagram_ctx_t*)v_stream_ctx;

    if (sender_ctx == NULL) {
        /* This should never happen, because the callback context for the is initialized 
         * when creating the sender. */
        return -1;
    }

    if (ret == 0) {
        switch (fin_or_event) {
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
        case picoquic_callback_stop_sending: 
        case picoquic_callback_stream_reset: 
        case picoquic_callback_stateless_reset:
        case picoquic_callback_close: 
        case picoquic_callback_application_close:
        case picoquic_callback_stream_gap:
            // None of these events should happen, because we cannot receive anything
            break;
        case picoquic_callback_prepare_to_send:
            // Currently, no STREAM frames are sent, just DATAGRAMs
            break;
        case picoquic_callback_prepare_datagram:
            /* Active sending API */
            if (datagram_ctx == NULL) {
                /* This should never happen */
            }
            else if (datagram_ctx->F == NULL) {
                /* Error, asking for data after end of file */
            }
            else {
                /* Implement the zero copy callback */
                size_t available = datagram_ctx->file_length - datagram_ctx->file_sent;
                uint8_t *buffer;

                if (available > length) {
                    available = length;
                }

                buffer = picoquic_provide_datagram_buffer(bytes, available);
                if (buffer != NULL) {
                    size_t nb_read = fread(buffer, 1, available, datagram_ctx->F);

                    if (nb_read != available) {
                        /* Error while reading the file */
                        multicast_sender_delete_datagram_context(sender_ctx, datagram_ctx);                    }
                    else {
                        datagram_ctx->file_sent += available;
                    }
                }
                else {
                    /* Should never happen according to callback spec. */
                    ret = -1;
                }
            }
            break;
        case picoquic_callback_almost_ready:
            break;
        case picoquic_callback_ready:
            break;
        default:
            /* unexpected -- just ignore. */
            break;
        }
    }

    return ret;
}

/* Prepare the context used by the multicast sender client:
 * - Create the QUIC context.
 * - Open the socket
 * - Find the server's address
 * - Prepare the multicast channel
 */
static int multicast_sender_init(int server_port, const char *server_cert, const char *server_key, 
    char const* file_path, picoquic_quic_t** quic, multicast_sender_ctx_t *sender_ctx,
    picoquic_multicast_channel_t* channel)
{
    int ret = 0;
    char const* qlog_dir = PICOQUIC_MULTICAST_CLIENT_QLOG_DIR;
    uint64_t current_time = picoquic_current_time();

    *quic = NULL;

    /* Create a QUIC context and:
     * - enable logging of encryption keys for wireshark debugging.
     * - instantiate a binary log option, and log all packets.
     */
    if (ret == 0) {
        *quic = picoquic_create(PICOQUIC_MULTICAST_MAX_CLIENTS, server_cert, server_key, NULL, PICOQUIC_MULTICAST_ALPN, 
            NULL, NULL, NULL, NULL, NULL, current_time, NULL, NULL, NULL, 0);

        if (*quic == NULL) {
            fprintf(stderr, "Could not create quic context\n");
            ret = -1;
        }
        else {
            picoquic_set_key_log_file_from_env(*quic);
            picoquic_set_qlog(*quic, qlog_dir);
            picoquic_set_log_level(*quic, 1);
            sender_ctx->quic = *quic;
        }
    }
    /* Initialize the callback context */
    if (ret == 0) {
        char text1[256];
        printf("Prepare multicast sending for group ip %s\n", 
            picoquic_addr_text((struct sockaddr*)&channel->group_ip, text1, sizeof(text1))
        );

        /* Set the callback context */
        picoquic_set_callback_multicast(channel, multicast_sender_callback, sender_ctx);
    }

    return ret;
}

/* Loop callback for the background client.
* 
* The main difference with the simpler loop used for the basic client
* is the support for the "wake up" callback, which we use to create
* streams inside the background thread because the picoquic code
* is not generally thread safe.
* 
* The support for the wakeup call is declared 
* 
* 
* In the "background client" mode, the files are requested from
* the UI thread, but the corresponding streams are created in the
* background thread. We use a very simple thread coordination
* process to keep the multicast simple:
* - the variable "nb_files" documents the number of files
*   that have been documented by the client. It is updated by
*   the UI thread when a new file is requested.
* - the variable "nb_files_processed" documents the number
*   of files for which a stream has been created. It is
*   updated by the UI thread.
* This is implemented in the "multicast process wakeup" function.
*/
// static int multicast_sender_wakeup(multicast_sender_ctx_t* client_ctx)
// {
//     int ret = 0;

//     while (client_ctx->nb_files > client_ctx->nb_files_processed) {
//         ret = multicast_sender_create_stream(client_ctx->cnx, client_ctx, client_ctx->nb_files_processed);
//         if (ret < 0) {
//             fprintf(stderr, "\nCould not initiate stream for file #%d, %s\n", 
//                 client_ctx->nb_files_processed,
//                 client_ctx->file_names[client_ctx->nb_files_processed]);
//             break;
//         }
//         client_ctx->nb_files_processed++;
//     }

//     // if (client_ctx->is_closing_requested && !client_ctx->is_closing) {
//     //     picoquic_close(client_ctx->cnx, 0);
//     // }

//     return ret;
// }

// TODO MC: Is this callback needed?
static int multicast_sender_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, 
    void* callback_ctx, void * callback_arg)
{
    int ret = 0;
    multicast_sender_ctx_t* sender_ctx = (multicast_sender_ctx_t*)callback_ctx;

    if (sender_ctx == NULL) {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    else {
        switch (cb_mode) {
        case picoquic_packet_loop_ready:
            break;
        case picoquic_packet_loop_wake_up:
            // ret = multicast_sender_wakeup(client_ctx);
            break;
        case picoquic_packet_loop_after_receive:
            break;
        case picoquic_packet_loop_after_send:
            if (sender_ctx->is_disconnected) {
                ret = PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }
            break;
        case picoquic_packet_loop_port_update:
            break;
        default:
            ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
            break;
        }
    }
    return ret;
}

/* Start multicast sender server */
int picoquic_multicast_sender_start(int server_port, 
    const char* server_cert, const char* server_key,
    char const* file_path, 
    picoquic_multicast_channel_t* channel, picoquic_network_thread_ctx_t* thread_ctx,
    multicast_sender_ctx_t* sender_ctx
) {
    int ret = 0;
    picoquic_quic_t* quic = NULL;
    int thread_ret = 0;
    picoquic_packet_loop_param_t param = { 0 };

    ret = multicast_sender_init(server_port, server_cert, server_key, 
        file_path, &quic, sender_ctx, channel);

    /* Initialize the file path field */
    sender_ctx->file_path = file_path;

    /* Set the multicast channel for this thread */
    param.multicast_channel = channel;

    /* Set the multicast sender port for this thread */
    param.local_port = server_port;

    /* Start the background thread. */
    thread_ctx = picoquic_start_custom_network_thread_ex(quic, &param,
        picoquic_internal_thread_create, picoquic_internal_thread_delete,
        picoquic_internal_thread_setname, "multicast_sender", 
        picoquic_packet_loop_multicast_send,
        multicast_sender_loop_cb,
        sender_ctx, &thread_ret);

    ret = multicast_sender_create_datagram(sender_ctx);

    return ret;
}

/* Stop multicast sender server and cleanup */
void picoquic_multicast_sender_stop(picoquic_network_thread_ctx_t* thread_ctx) {
    multicast_sender_ctx_t* sender_ctx = (multicast_sender_ctx_t*)thread_ctx->loop_callback_ctx;
    picoquic_quic_t* quic = sender_ctx->quic;
    
    /* close the network thread. */
    picoquic_delete_network_thread(thread_ctx);
    thread_ctx = NULL;

    /* Save tickets and tokens, and free the QUIC context */
    if (quic != NULL) {
        picoquic_free(quic);
    }

    /* Free the Client context */
    multicast_sender_free_context(sender_ctx);
}