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
 * instantiated in client or server mode. The "dgramspl_client" implements
 * the client components of the dgramspl application.
 *
 * Developing the client requires two main components:
 *  - the client "callback" that implements the client side of the
 *    application protocol, managing the client side application context
 *    for the connection.
 *  - the client loop, that reads messages on the socket, submits them
 *    to the Quic context, let the client prepare messages, and send
 *    them on the appropriate socket.
 *
 * The dgramspl Client uses the "qlog" option to produce Quic Logs as defined
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
#include "dgramspl.h"

 /* Client context and callback management:
  *
  * The client application context is created before the connection
  * is created. It contains the list of files that will be required
  * from the server.
  * On initial start, the client creates all the stream contexts 
  * that will be needed for the requested files, and marks all
  * these contexts as active.
  * Each stream context includes:
  *  - description of the stream state:
  *      name sent or not, FILE open or not, stream reset or not,
  *      stream finished or not.
  *  - index of the file in the list.
  *  - number of file name bytes sent.
  *  - stream ID.
  *  - the FILE pointer for reading the data.
  * Server side stream context is created when the client starts the
  * stream. It is closed when the file transmission
  * is finished, or when the stream is abandoned.
  *
  * The server side callback is a large switch statement, with one entry
  * for each of the call back events.
  */

typedef struct st_dgramspl_client_ctx_t {
    picoquic_cnx_t* cnx;
    picoquic_tp_t transport_parameters;
    char const* default_dir;
    FILE *F;
    size_t bytes_received;
    unsigned int is_disconnected : 1;
} dgramspl_client_ctx_t;

static void dgramspl_client_free_context(dgramspl_client_ctx_t* client_ctx)
{
    if (client_ctx->F != NULL) {
        (void)picoquic_file_close(client_ctx->F);
    }
}

int dgramspl_client_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    dgramspl_client_ctx_t* client_ctx = (dgramspl_client_ctx_t*)callback_ctx;

    if (client_ctx == NULL) {
        /* This should never happen, because the callback context for the client is initialized 
         * when creating the client connection. */
        return -1;
    }

    if (ret == 0) {
        switch (fin_or_event) {
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
            /* Received STREAM data, should not happen on this client */
            break;
        case picoquic_callback_datagram:
            /* Data arrival on datagram */

            int is_final_datagram = 0; // close file and mark as finished when file suffix detected
            int header_length = PICOQUIC_DGRAMSPL_DATAGRAM_HEADER_LENGTH;

            int is_last = (*(bytes) >> (uint8_t) 0) & (uint8_t) 1;
            int is_first = (*(bytes) >> (uint8_t) 1) & (uint8_t) 1;

            uint64_t fragment_number;
            memcpy(&fragment_number, (bytes + 1), 8);

            if (is_last == 1) {
                is_final_datagram = 1;
            }

            if (client_ctx->F == NULL)
            {
                /* Open the file to receive the data. This is done at the last possible moment,
                * to minimize the number of files open simultaneously.
                * When formatting the file_path, verify that the directory name is zero-length,
                * or terminated by a proper file separator.
                */
                char file_path[1024];
                size_t dir_len = strlen(client_ctx->default_dir);
                size_t file_name_len = strlen(PICOQUIC_DGRAMSPL_CLIENT_DATA_FILENAME);

                if (dir_len > 0 && dir_len < sizeof(file_path))
                {
                    memcpy(file_path, client_ctx->default_dir, dir_len);
                    if (file_path[dir_len - 1] != PICOQUIC_FILE_SEPARATOR[0])
                    {
                        file_path[dir_len] = PICOQUIC_FILE_SEPARATOR[0];
                        dir_len++;
                    }
                }

                if (dir_len + file_name_len + 1 >= sizeof(file_path))
                {
                    /* Unexpected: could not format the file name */
                    fprintf(stderr, "app: Could not format the file path.\n");
                    ret = -1;
                }
                else
                {
                    memcpy(file_path + dir_len, PICOQUIC_DGRAMSPL_CLIENT_DATA_FILENAME, file_name_len);
                    file_path[dir_len + file_name_len] = 0;
                    client_ctx->F = picoquic_file_open(file_path, "wb");

                    if (client_ctx->F == NULL)
                    {
                        /* Could not open the file */
                        fprintf(stderr, "app: Could not open the file: %s\n", file_path);
                        ret = -1;
                    }
                }
            }

            // TODO MC: The delivery of DATAGRAM frames is not in order. Add logic to store fragment to disk in order
            if (ret == 0 && length > 0)
            {
                fprintf(stdout, "GOT datagram (fragment number: %lu, is_first: %i, is_last: %i) in application\n", fragment_number, is_first, is_last);

                /* write the received bytes to the file */
                if (fwrite(bytes + header_length, length - header_length, 1, client_ctx->F) != 1)
                {
                    /* Could not write file to disk */
                    fprintf(stderr, "app: Could not write data to disk.\n");
                    ret = -1;
                }
                else
                {
                    client_ctx->bytes_received += length;
                }
            }
            
            // TODO MC: Not just close file and connection when receiving the final datagram, other packets with lower fragment number could arrive afterwards (unordered delivery)
            if (ret == 0 && is_final_datagram == 1)
            {
                fprintf(stdout, "TRANSMISSION COMPLETE, CLOSING FILE\n");
                client_ctx->F = picoquic_file_close(client_ctx->F);
            }
            break;
        case picoquic_callback_stop_sending: /* Should not happen, treated as reset */
        case picoquic_callback_stream_reset: /* Server reset stream #x */
            break;
        case picoquic_callback_stateless_reset:
        case picoquic_callback_close: /* Received connection close */
            fprintf(stdout, "picoquic_callback_close.\n");
        case picoquic_callback_application_close: /* Received application close */
            fprintf(stdout, "Connection closed.\n");
            /* Mark the connection as completed */
            client_ctx->is_disconnected = 1;
            /* Remove the application callback */
            picoquic_set_callback(cnx, NULL, NULL);
            break;
        case picoquic_callback_version_negotiation:
            /* The client did not get the right version.
             * TODO: some form of negotiation?
             */
            fprintf(stdout, "Received a version negotiation request:");
            for (size_t byte_index = 0; byte_index + 4 <= length; byte_index += 4) {
                uint32_t vn = 0;
                for (int i = 0; i < 4; i++) {
                    vn <<= 8;
                    vn += bytes[byte_index + i];
                }
                fprintf(stdout, "%s%08x", (byte_index == 0) ? " " : ", ", vn);
            }
            fprintf(stdout, "\n");
            break;
        case picoquic_callback_stream_gap:
            /* This callback is never used. */
            break;
        case picoquic_callback_prepare_to_send:
            /* Prepare to send STREAM data, will never happen on the client */
            break;
        case picoquic_callback_almost_ready:
            fprintf(stdout, "Connection to the server completed, almost ready.\n");
            break;
        case picoquic_callback_ready:
            /* TODO: Check that the transport parameters are what the dgramspl expects */
            fprintf(stdout, "Connection to the server confirmed.\n");
            break;
        default:
            /* unexpected -- just ignore. */
            break;
        }
    }

    return ret;
}

/* Sample client,  loop call back management.
 * The function "picoquic_packet_loop" will call back the application when it is ready to
 * receive or send packets, after receiving a packet, and after sending a packet.
 * We implement here a minimal callback that instruct  "picoquic_packet_loop" to exit
 * when the connection is complete.
 */

static int dgramspl_client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, 
    void* callback_ctx, void * callback_arg)
{
    int ret = 0;
    dgramspl_client_ctx_t* cb_ctx = (dgramspl_client_ctx_t*)callback_ctx;

    if (cb_ctx == NULL) {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    else {
        switch (cb_mode) {
        case picoquic_packet_loop_ready:
            fprintf(stdout, "Waiting for packets.\n");
            break;
        case picoquic_packet_loop_after_receive:
            break;
        case picoquic_packet_loop_after_send:
            if (picoquic_get_cnx_state(cb_ctx->cnx) == picoquic_state_disconnected || cb_ctx->is_disconnected) {
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

/* Prepare the context used by the simple client:
 * - Create the QUIC context.
 * - Open the sockets
 * - Find the server's address
 * - Initialize the client context and create a client connection.
 */
static int dgramspl_client_init(char const* server_name, int server_port, char const* default_dir,
    char const* ticket_store_filename, char const* token_store_filename,
    struct sockaddr_storage * server_address, picoquic_quic_t** quic, picoquic_cnx_t** cnx, dgramspl_client_ctx_t *client_ctx)
{
    int ret = 0;
    char const* sni = PICOQUIC_DGRAMSPL_SNI;
    char const* qlog_dir = PICOQUIC_DGRAMSPL_CLIENT_QLOG_DIR;
    uint64_t current_time = picoquic_current_time();

    *quic = NULL;
    *cnx = NULL;

    /* Get the server's address */
    if (ret == 0) {
        int is_name = 0;

        ret = picoquic_get_server_address(server_name, server_port, server_address, &is_name);
        if (ret != 0) {
            fprintf(stderr, "Cannot get the IP address for <%s> port <%d>", server_name, server_port);
        }
        else if (is_name) {
            sni = server_name;
        }
    }

    /* Create a QUIC context. It could be used for many connections, but in this dgramspl we
     * will use it for just one connection.
     * The dgramspl code exercises just a small subset of the QUIC context configuration options:
     * - use files to store tickets and tokens in order to manage retry and 0-RTT
     * - set the congestion control algorithm to BBR
     * - enable logging of encryption keys for wireshark debugging.
     * - instantiate a binary log option, and log all packets.
     */
    if (ret == 0) {
        *quic = picoquic_create(1, NULL, NULL, NULL, PICOQUIC_DGRAMSPL_ALPN, NULL, NULL,
            NULL, NULL, NULL, current_time, NULL,
            ticket_store_filename, NULL, 0);

        if (*quic == NULL) {
            fprintf(stderr, "Could not create quic context\n");
            ret = -1;
        }
        else {
            if (picoquic_load_retry_tokens(*quic, token_store_filename) != 0) {
                fprintf(stderr, "No token file present. Will create one as <%s>.\n", token_store_filename);
            }

            picoquic_set_default_congestion_algorithm(*quic, picoquic_bbr_algorithm);

            picoquic_set_key_log_file_from_env(*quic);
            picoquic_set_qlog(*quic, qlog_dir);
            picoquic_set_log_level(*quic, 1);
        }
    }
    /* Initialize the callback context and create the connection context.
     * We use minimal options on the client side, keeping the transport
     * parameter values set by default for picoquic. This could be fixed later.
     */

    if (ret == 0) {
        client_ctx->default_dir = default_dir;

        printf("Starting connection to %s, port %d\n", server_name, server_port);

        /* Create a client connection */
        *cnx = picoquic_create_cnx(*quic, picoquic_null_connection_id, picoquic_null_connection_id,
            (struct sockaddr*)server_address, current_time, 0, sni, PICOQUIC_DGRAMSPL_ALPN, 1);

        if (*cnx == NULL) {
            fprintf(stderr, "Could not create connection context\n");
            ret = -1;
        }
        else {
            /* Document connection in client's context */
            client_ctx->cnx = *cnx;

            /* Activate the datagram extension */
            picoquic_init_transport_parameters(&client_ctx->transport_parameters, 1);
            client_ctx->transport_parameters.max_datagram_frame_size = PICOQUIC_MAX_PACKET_SIZE;
            picoquic_set_transport_parameters(*cnx, &client_ctx->transport_parameters);

            // TODO MC: Set max rate somewhere for comparison to multicast 

            /* Set the client callback context */
            picoquic_set_callback(*cnx, dgramspl_client_callback, client_ctx);

            /* Client connection parameters could be set here, before starting the connection. */
            ret = picoquic_start_client_cnx(*cnx);

            if (ret < 0) {
                fprintf(stderr, "Could not activate connection\n");
            } else {
                /* Printing out the initial CID, which is used to identify log files */
                picoquic_connection_id_t icid = picoquic_get_initial_cnxid(*cnx);
                printf("Initial connection ID: ");
                for (uint8_t i = 0; i < icid.id_len; i++) {
                    printf("%02x", icid.id[i]);
                }
                printf("\n");
            }
        }
    }

    return ret;
}

/* Client:
 * - Call the init function to:
 *    - Create the QUIC context.
 *    - Open the sockets
 *    - Find the server's address
 *    - Create a client context and a client connection.
 * - Initialize the list of required files based on the CLI parameters.
 * - On a forever loop:
 *     - get the next wakeup time
 *     - wait for arrival of message on sockets until that time
 *     - if a message arrives, process it.
 *     - else, check whether there is something to send.
 *       if there is, send it.
 * - The loop breaks if the client connection is finished.
 */

int dgramspl_client(char const * server_name, int server_port, char const * default_dir)
{
    int ret = 0;
    struct sockaddr_storage server_address;
    picoquic_quic_t* quic = NULL;
    picoquic_cnx_t* cnx = NULL;
    dgramspl_client_ctx_t client_ctx = { 0 };
    picoquic_packet_loop_param_t param = {0};
    char const* ticket_store_filename = PICOQUIC_DGRAMSPL_CLIENT_TICKET_STORE;
    char const* token_store_filename = PICOQUIC_DGRAMSPL_CLIENT_TOKEN_STORE;

    ret = dgramspl_client_init(server_name, server_port, default_dir,
        ticket_store_filename, token_store_filename,
        &server_address, &quic, &cnx, &client_ctx);

    param.local_af = server_address.ss_family;
    param.local_port = (uint16_t)picoquic_uniform_random(30000) + 20000;

    /* Wait for packets */
    ret = picoquic_packet_loop_v2(quic, &param, dgramspl_client_loop_cb, &client_ctx);

    /* Save tickets and tokens, and free the QUIC context */
    if (quic != NULL) {
        if (picoquic_save_session_tickets(quic, ticket_store_filename) != 0) {
            fprintf(stderr, "Could not store the saved session tickets.\n");
        }
        if (picoquic_save_retry_tokens(quic, token_store_filename) != 0) {
            fprintf(stderr, "Could not save tokens to <%s>.\n", token_store_filename);
        }
        picoquic_free(quic);
    }

    /* Free the Client context */
    dgramspl_client_free_context(&client_ctx);

    return ret;
}
