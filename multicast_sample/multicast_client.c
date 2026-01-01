/* The "multicast" project builds a simple multicast application that can be
 * instantiated in client or server mode. The "multicast_client" implements
 * the client components of the multicast application.
 * 
 * The multicast client currently only receives DATAGRAM frames, where each DATAGRAM frame 
 * contains a small custom application header defining a "fragment number" (so that the 
 * client can rearrange the DATAGRAM frames into the correct order again) and an indicator
 * if this is the first or the last fragment of the sent file.
 *
 * The multicast server uses the "qlog" option to produce Quic Logs as defined
 * in https://datatracker.ietf.org/doc/draft-marx-qlog-event-definitions-quic-h3/.
 * This is an optional feature, which requires linking with the "loglib" library,
 * and using the picoquic_set_qlog() API defined in "autoqlog.h". . When a connection
 * completes, the code saves the log as a file named after the Initial Connection
 * ID (in hexa), with the suffix ".server.qlog".
 */

#include <stdint.h>
#include <stdio.h>
#include <picoquic.h>
#include "picoquic_internal.h"
#include <picoquic_utils.h>
#include <picosocks.h>
#include <autoqlog.h>
#include <picoquic_packet_loop.h>
#include "picoquic_multicast.h"

/* Client context and callback management:
 *
 * The client application context is created before the connection
 * is created.
 * 
 * The server side callback is a large switch statement, with one entry
 * for each of the call back events.
 */
typedef struct st_multicast_client_ctx_t
{
    picoquic_cnx_t *cnx;
    char const *default_dir;
    struct sockaddr_storage server_address;
    struct sockaddr_storage multicast_group_address;
    int nb_files_received;
    picoquic_tp_multicast_client_params_t* tp_params;

    FILE *F;
    size_t bytes_received;
    unsigned int is_stream_finished : 1;
} multicast_client_ctx_t;

/* Close file and free context */
static void multicast_client_free_context(multicast_client_ctx_t *client_ctx)
{
    if (client_ctx->F != NULL) {
        (void)picoquic_file_close(client_ctx->F);
    }
    if (client_ctx->tp_params != NULL) {
        free(client_ctx->tp_params);
    }
}

/* Application callback */
int multicast_client_callback_multicast(picoquic_multicast_channel_t* channel, 
    uint64_t stream_id, uint8_t *bytes, size_t length, 
    picoquic_call_back_event_t fin_or_event, void *callback_ctx, void *v_stream_ctx
) {
    int ret = 0;
    multicast_client_ctx_t *client_ctx = (multicast_client_ctx_t *)callback_ctx;

    if (client_ctx == NULL)
    {
        fprintf(stdout, "Try to receive datagram: client_ctx or stream_ctx is NULL, skipping\n");
        /* This should never happen, because the callback context for the client is initialized
         * when creating the client connection. */
        return -1;
    }

    if (ret == 0)
    {
        switch (fin_or_event)
        {
            case picoquic_callback_multicast_datagram:
                /* Data arrival on datagram */
                
                int is_final_datagram = 0; // close file and mark as finished when file suffix detected
                int header_length = PICOQUIC_MULTICAST_DATAGRAM_HEADER_LENGTH;

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
                    size_t file_name_len = strlen(PICOQUIC_MULTICAST_CLIENT_DATA_FILENAME);

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
                        memcpy(file_path + dir_len, PICOQUIC_MULTICAST_CLIENT_DATA_FILENAME, file_name_len);
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
                    fprintf(stdout, "GOT multicast datagram (fragment number: %lu, is_first: %i, is_last: %i) in application\n", fragment_number, is_first, is_last);

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
                    
                    client_ctx->is_stream_finished = 1;
                    client_ctx->nb_files_received++;

                    // TODO MC: Maybe already leave the channel here (but wait for the missing frames though!)
                }
                break;
            default:
                break;
        }
    }

    return ret;
}

/* Packet Loop Callback */
int multicast_client_callback(picoquic_cnx_t *cnx,
                              uint64_t stream_id, uint8_t *bytes, size_t length,
                              picoquic_call_back_event_t fin_or_event, void *callback_ctx, void *v_stream_ctx)
{
    int ret = 0;
    multicast_client_ctx_t *client_ctx = (multicast_client_ctx_t *)callback_ctx;

    if (client_ctx == NULL)
    {
        /* This should never happen, because the callback context for the client is initialized
         * when creating the client connection. */
        return -1;
    }

    if (ret == 0)
    {
        switch (fin_or_event)
        {
        case picoquic_callback_path_available:
            break;
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
            /* STREAM data arrival, will not happen in the current scenario */
            break;
        case picoquic_callback_stop_sending: /* Should not happen, treated as reset */
            /* Mark stream as abandoned, close the file, etc. */
            picoquic_reset_stream(cnx, stream_id, 0);
        case picoquic_callback_stream_reset: /* Server reset stream #x */
            break;
        case picoquic_callback_stateless_reset:
            fprintf(stdout, "app: Received a stateless reset.\n");
            break;
        case picoquic_callback_close:
            fprintf(stdout, "app: Received request to close connection\n");
            break;
        case picoquic_callback_application_close:
            fprintf(stdout, "app: Received request to close application.\n");
            /* Remove the application callback */
            picoquic_set_callback(cnx, NULL, NULL);
            break;
        case picoquic_callback_version_negotiation:
            /* The client did not get the right version.
             * TODO: some form of negotiation?
             */
            fprintf(stdout, "app: Received a version negotiation request:");
            for (size_t byte_index = 0; byte_index + 4 <= length; byte_index += 4)
            {
                uint32_t vn = 0;
                for (int i = 0; i < 4; i++)
                {
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
            fprintf(stdout, "app: Connection to the server completed, almost ready.\n");
            break;
        case picoquic_callback_ready:
            /* TODO: Check that the transport parameters are what the application expects */
            fprintf(stdout, "app: Connection to the server confirmed.\n");
            break;
        case picoquic_callback_multicast_join_possible:
            fprintf(stdout, "App: Multicast join possible\n");
            picoquic_multicast_channel_id_t *ch_id = (picoquic_multicast_channel_id_t *)v_stream_ctx;
            ret = picoquic_join_mc_channel(cnx, ch_id); // For now, always accept join request by server
            picoquic_mc_channel_in_cnx_t* ch_in_cnx = picoquic_find_multicast_channel_in_cnx(ch_id, cnx);
            picoquic_set_callback_multicast(ch_in_cnx->channel, multicast_client_callback_multicast, callback_ctx);
            break;
        default:
            /* unexpected -- just ignore. */
            break;
        }
    }

    return ret;
}

/* Multicast client, loop call back management.
 * The function "picoquic_packet_loop" will call back the application when it is ready to
 * receive or send packets, after receiving a packet, and after sending a packet.
 * We implement here a minimal callback that instruct "picoquic_packet_loop" to exit
 * when the connection is complete.
 */
static int multicast_client_loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum cb_mode,
                                    void *callback_ctx, void *callback_arg)
{
    int ret = 0;
    multicast_client_ctx_t *cb_ctx = (multicast_client_ctx_t *)callback_ctx;

    if (cb_ctx == NULL)
    {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    else
    {
        switch (cb_mode)
        {
        case picoquic_packet_loop_ready:
        case picoquic_packet_loop_after_receive:
        case picoquic_packet_loop_port_update:
        case picoquic_packet_loop_alt_port:
            break;
        case picoquic_packet_loop_after_send:
            if (picoquic_get_cnx_state(cb_ctx->cnx) == picoquic_state_disconnected) {
                ret = PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }
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
static int multicast_client_init(char const *server_name, int server_port, char const *default_dir, int max_rate,
                                 char const *ticket_store_filename, char const *token_store_filename,
                                 struct sockaddr_storage *server_address, picoquic_quic_t **quic, picoquic_cnx_t **cnx, multicast_client_ctx_t *client_ctx)
{
    int ret = 0;
    char const *sni = PICOQUIC_MULTICAST_SNI;
    char const *qlog_dir = PICOQUIC_MULTICAST_CLIENT_QLOG_DIR;
    uint64_t current_time = picoquic_current_time();

    *quic = NULL;
    *cnx = NULL;

    /* Get the server's address */
    if (ret == 0) {
        int is_name = 0;

        ret = picoquic_get_server_address(server_name, server_port, server_address, &is_name);
        if (ret != 0) {
            fprintf(stderr, "init: Cannot get the IP address for <%s> port <%d>", server_name, server_port);
        } else if (is_name) {
            sni = server_name;
        }
    }

    /* Create a QUIC context. It could be used for many connections, but in this application we
     * will use it for just one connection. */
    if (ret == 0) {
        *quic = picoquic_create(1, NULL, NULL, NULL, PICOQUIC_MULTICAST_ALPN, NULL, NULL,
                                NULL, NULL, NULL, current_time, NULL,
                                ticket_store_filename, NULL, 0);

        if (*quic == NULL) {
            fprintf(stderr, "init: Could not create quic context\n");
            ret = -1;
        } else {
            if (picoquic_load_retry_tokens(*quic, token_store_filename) != 0) {
                fprintf(stderr, "init: No token file present. Will create one as <%s>.\n", token_store_filename);
            }

            picoquic_set_default_congestion_algorithm(*quic, picoquic_bbr_algorithm);
            picoquic_enable_sslkeylog(*quic, 1);
            picoquic_set_key_log_file_from_env(*quic);
            picoquic_set_qlog(*quic, qlog_dir);
            picoquic_set_log_level(*quic, 1);
            picoquic_enable_path_callbacks_default(*quic, 1);

            // Multicast settings
            client_ctx->tp_params = malloc(sizeof(picoquic_tp_multicast_client_params_t));
            memset(client_ctx->tp_params, 0, sizeof(picoquic_tp_multicast_client_params_t));
            client_ctx->tp_params->max_aggregate_rate = (uint64_t)max_rate;

            picoquic_set_default_multicast_option(*quic, 1);
            picoquic_set_default_multicast_client_params(*quic, client_ctx->tp_params);
            printf("init: Accept multicast: %s.\n", ((*quic)->default_multicast_option) ? "Yes" : "No");
        }
    }

    /* Initialize the callback context and create the connection context.
     * We use minimal options on the client side, keeping the transport
     * parameter values set by default for picoquic. This could be fixed later.
     */
    if (ret == 0) {
        client_ctx->default_dir = default_dir;

        printf("init: Starting connection to %s, port %d\n", server_name, server_port);

        /* Create a client connection */
        *cnx = picoquic_create_cnx(*quic, picoquic_null_connection_id, picoquic_null_connection_id,
                                   (struct sockaddr *)server_address, current_time, 0, sni, PICOQUIC_MULTICAST_ALPN, 1);

        if (*cnx == NULL) {
            fprintf(stderr, "init: Could not create connection context\n");
            ret = -1;
        } else {
            /* Document connection in client's context */
            client_ctx->cnx = *cnx;

            /* Set the client callback context */
            picoquic_set_callback(*cnx, multicast_client_callback, client_ctx);

            /* Client connection parameters could be set here, before starting the connection. */
            ret = picoquic_start_client_cnx(*cnx);

            if (ret < 0) {
                fprintf(stderr, "init: Could not activate connection\n");
            } else {
                /* Printing out the initial CID, which is used to identify log files */
                picoquic_connection_id_t icid = picoquic_get_initial_cnxid(*cnx);
                printf("init: Initial connection ID: ");
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
int picoquic_multicast_client(char const *server_name, int server_port, char const *default_dir, int max_rate)
{
    int ret = 0;
    struct sockaddr_storage server_address;
    picoquic_quic_t *quic = NULL;
    picoquic_cnx_t *cnx = NULL;
    multicast_client_ctx_t client_ctx = {0};
    picoquic_packet_loop_param_t param = {0};
    char const *ticket_store_filename = PICOQUIC_MULTICAST_CLIENT_TICKET_STORE;
    char const *token_store_filename = PICOQUIC_MULTICAST_CLIENT_TOKEN_STORE;

    ret = multicast_client_init(server_name, server_port, default_dir, max_rate,
                                ticket_store_filename, token_store_filename,
                                &server_address, &quic, &cnx, &client_ctx);

    if (ret == 0) {
        /* Initialize all the streams contexts from the list of streams passed on the API. */
        client_ctx.server_address = server_address;
    }

    param.local_af = server_address.ss_family;
    param.local_port = (uint16_t)picoquic_uniform_random(30000) + 20000;
    param.extra_socket_required = 1;
    param.prefer_extra_socket = 0;

    /* Wait for packets */
    ret = picoquic_packet_loop_v2(quic, &param, multicast_client_loop_cb, &client_ctx);

    /* Save tickets and tokens, and free the QUIC context */
    if (quic != NULL) {
        if (picoquic_save_session_tickets(quic, ticket_store_filename) != 0) {
            fprintf(stderr, "client: Could not store the saved session tickets.\n");
        }
        if (picoquic_save_retry_tokens(quic, token_store_filename) != 0) {
            fprintf(stderr, "client: Could not save tokens to <%s>.\n", token_store_filename);
        }
        picoquic_free(quic);
    }

    /* Free the Client context */
    multicast_client_free_context(&client_ctx);

    return ret;
}
