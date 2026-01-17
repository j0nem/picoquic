/* The "multicast" project builds a simple multicast application that can be
 * instantiated in client or server mode. The "multicast_server" and "multicast_sender" implements
 * the server components of the multicast application.
 *
 * The server currently works using two components:
 * 
 *  - the multicast control server (multicast_server.c) records the multicast channel state
 *    of the clients (joined/left) and handles the lifecycle of the multicast sender (start, stop).
 *    It is started directly when "multicast server <params>" is executed, initializes picoquic,
 *    sets the picoquic callbacks for client interaction and starts the packet loop.
 *
 *  - the multicast sender (multicast_sender.c) runs in a separate thread within the server application
 *    and is started by the control server, once a specified threshold of clients is reached 
 *    (clients_threshold CLI parameter). The sender runs with the same quic context as the control 
 *    server and "stupidly" sends out data from a file specified by the control server via a special 
 *    send-only packet loop. Once the sender is finished with sending the file, it calls a specified 
 *    callback in the control server. The control server then invokes retiring of all clients for that 
 *    multicast chanenl and stops the sender thread.
 * 
 * The multicast sender currently only sends out DATAGRAM frames contining the specified file,
 * where each DATAGRAM frame contains a small custom application header defining a "fragment number"
 * (so that the clients can rearrange the DATAGRAM frames into the correct order again) and an indicator
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
#include <picosocks.h>
#include <picoquic_utils.h>
#include <autoqlog.h>
#include "picoquic.h"
#include "picoquic_internal.h"
#include "picoquic_multicast.h"
#include "picoquic_packet_loop.h"

typedef struct st_multicast_server_cnx_ctx_t
{
    multicast_server_ctx_t* global_ctx;
    int is_joined;
    int is_active;
} multicast_server_cnx_ctx_t;

/* Delete sender context */
void multicast_server_delete_sender_context(multicast_sender_ctx_t *sender_ctx)
{
    picoquic_delete_network_thread(sender_ctx->thread_ctx);
    sender_ctx->thread_ctx = NULL;

    /* free the sender application context */
    if (sender_ctx->F != NULL) {
        (void)picoquic_file_close(sender_ctx->F);
    }
}

/* Server proactively removes a specific client from channel */
// CHECK MC: Currently, this is not used, and also not needed in the example application,
// as all clients are removed at once after the file transfer is finished using picoquic_schedule_mc_leave_and_retire
void multicast_server_remove_client_from_channel(multicast_server_cnx_ctx_t* server_cnx_ctx, picoquic_cnx_t* cnx) {
    multicast_server_ctx_t* server_ctx = server_cnx_ctx->global_ctx;

    if (server_cnx_ctx == NULL || server_ctx == NULL) {
        // error
        fprintf(stderr, "Error: Client could not be removed from channel\n");
        return;
    }

    if (server_ctx->nb_joined_clients > 0) {
        picoquic_schedule_mc_leave(server_ctx->mc_channel, cnx);
    } else {
        picoquic_schedule_mc_retire(server_ctx->mc_channel, cnx);
    }
    if (server_cnx_ctx->is_active) {
        server_cnx_ctx->is_active = 0;
        server_ctx->nb_active_clients--;
    }
}

/* Handle STATE(LEFT) or STATE(RETIRED) from client */
void multicast_server_process_client_left_or_retired(multicast_server_cnx_ctx_t* server_cnx_ctx, picoquic_cnx_t* cnx) {
    multicast_server_ctx_t* server_ctx = server_cnx_ctx->global_ctx;

    if (server_cnx_ctx->is_active) {
        server_cnx_ctx->is_active = 0;
        server_ctx->nb_active_clients--;
    }
    if (server_cnx_ctx->is_joined) {
        server_cnx_ctx->is_joined = 0;
        server_ctx->nb_joined_clients--;
    }

    fprintf(stdout, "Kick out client, joined clients new: %i, active clients new: %i\n", server_ctx->nb_joined_clients, server_ctx->nb_active_clients);

    free(server_cnx_ctx);
    picoquic_set_callback(cnx, NULL, NULL);
    
    // Stop multicast sender if all clients are already left
    if (server_ctx != NULL && server_ctx->nb_joined_clients == 0 && server_ctx->mc_channel->is_retired) {
        server_ctx->is_closing_sender = 1;
    }
}

/*
 * The server side callback is a large switch statement, with one entry
 * for each of the call back events.
 */
int multicast_server_callback(picoquic_cnx_t *cnx,
                              uint64_t stream_id, uint8_t *bytes, size_t length,
                              picoquic_call_back_event_t fin_or_event, void *callback_ctx, void *v_stream_ctx)
{
    int ret = 0;
    multicast_server_cnx_ctx_t *server_cnx_ctx = (multicast_server_cnx_ctx_t *)callback_ctx;
    multicast_server_ctx_t *server_ctx = server_cnx_ctx->global_ctx;
    
    if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx)))
    {
        server_cnx_ctx = (multicast_server_cnx_ctx_t *)malloc(sizeof(multicast_server_cnx_ctx_t));
        if (server_cnx_ctx == NULL)
        {
            /* cannot handle the connection */
            picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
            return -1;
        }
        else
        {
            multicast_server_cnx_ctx_t *d_ctx = (multicast_server_cnx_ctx_t *)picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx));
            if (d_ctx != NULL)
            {
                memcpy(server_cnx_ctx, d_ctx, sizeof(multicast_server_cnx_ctx_t));
            }
            else
            {
                /* This really is an error case: the default connection context should never be NULL */
                memset(server_cnx_ctx, 0, sizeof(multicast_server_cnx_ctx_t));
            }
            picoquic_set_callback(cnx, multicast_server_callback, server_cnx_ctx);
        }
    }

    if (ret == 0)
    {
        switch (fin_or_event)
        {
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
            // STREAM data arrived. Should not be the case here
            break;
        case picoquic_callback_prepare_to_send:
            // STREAM data should be send. Not the case here
            break;
        case picoquic_callback_stream_reset: /* Client reset stream #x */
        case picoquic_callback_stop_sending: /* Client asks server to reset stream #x */
        case picoquic_callback_stateless_reset:   /* Received an error message */
        case picoquic_callback_close:             /* Received connection close */
        case picoquic_callback_application_close: /* Received application close */
            // Treat all of this as if the client would have sent STATE(LEFT) or STATE(RETIRED)
            multicast_server_process_client_left_or_retired(server_cnx_ctx, cnx);
            if (server_ctx != NULL) {
                fprintf(stdout, "Callback to close a connection, joined clients new: %i\n", server_ctx->nb_joined_clients);
            } else {
                fprintf(stdout, "Callback to close a connection, nobody left, stopping.\n");
            }
            break;
        case picoquic_callback_version_negotiation:
            /* The server should never receive a version negotiation response */
            break;
        case picoquic_callback_stream_gap:
            /* This callback is never used. */
            break;
        case picoquic_callback_almost_ready:
        case picoquic_callback_ready:
            /* Check that the transport parameters are what the multicast expects */
            if (cnx->is_multicast_enabled == 1 && server_ctx->mc_channel != NULL && cnx->nb_mc_channels == 0 
                && server_ctx->nb_joined_clients < PICOQUIC_MULTICAST_MAX_CLIENTS) {
                // send announce, key and join frame to client
                server_cnx_ctx->is_joined = 1;
                server_ctx->nb_joined_clients++;
                picoquic_schedule_mc_announce_and_join(cnx, server_ctx->mc_channel);
            }
            fprintf(stdout, "Callback picoquic_callback_ready, joined clients new: %i\n", server_ctx->nb_joined_clients);
            break;
        case picoquic_callback_multicast_join_attempted:
            // If data sending on multicast channel did not start yet, start now
            fprintf(stdout, "Callback picoquic_callback_multicast_join_attempted, joined clients: %i, threshold: %i\n", server_ctx->nb_joined_clients, server_ctx->client_threshold);
            if (!server_ctx->sender_running && server_ctx->nb_joined_clients >= server_ctx->client_threshold) {
                int err = multicast_sender_start(&server_ctx->sender_app_ctx, server_ctx->sender_thread_ret, server_ctx->sender_loop_params);
                if (err == 0) {
                    server_ctx->sender_running = 1;
                    fprintf(stdout, "Multicast sender started\n");
                } else {
                    fprintf(stderr, "Error while starting multicast sender: %i\n", err);
                }
            }
            break;
        case picoquic_callback_multicast_join_confirmed:
            if (server_cnx_ctx->is_joined) {
                server_cnx_ctx->is_active = 1;
                server_ctx->nb_active_clients++;    
                fprintf(stdout, "Callback picoquic_callback_multicast_join_confirmed, active clients new: %i\n", server_ctx->nb_active_clients);
            } else {
                fprintf(stdout, "Callback picoquic_callback_multicast_join_confirmed, client not joined anymore\n");
            }
            break;
        case picoquic_callback_multicast_left:
        case picoquic_callback_multicast_retired:
            // Client has sent STATE(LEFT) or STATE(RETIRED)
            // ENHANCE MC: Differentiation between STATE(LEFT) and STATE(RETIRED) needed?
            multicast_server_process_client_left_or_retired(server_cnx_ctx, cnx);
            if (server_ctx != NULL) {
                fprintf(stdout, "Callback to close a connection, joined clients new: %i\n", server_ctx->nb_joined_clients);
            } else {
                fprintf(stdout, "Callback to close a connection, nobody left, stopping.\n");
            }
            break;
        default:
            /* unexpected */
            break;
        }
    }

    return ret;
}

static int multicast_server_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, 
    void* callback_ctx, void * callback_arg)
{
    int ret = 0;
    multicast_server_ctx_t* server_ctx = (multicast_server_ctx_t*)callback_ctx;

    if (server_ctx == NULL) {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }
    else {
        switch (cb_mode) {
        case picoquic_packet_loop_ready:
        case picoquic_packet_loop_wake_up:
        case picoquic_packet_loop_after_receive:
        case picoquic_packet_loop_port_update:
            break;
        case picoquic_packet_loop_after_send:
            if (server_ctx->is_closing_server) {
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

int multicast_server(int server_port, int sender_port, const char *server_cert, const char *server_key, int max_rate, const char *served_file, int client_threshold, int is_local)
{
    /* Start: start the QUIC process with cert and key files */
    int ret = 0;
    picoquic_quic_t *quic = NULL;
    char const *qlog_dir = PICOQUIC_MULTICAST_SERVER_QLOG_DIR;
    uint64_t current_time = 0;
    multicast_server_cnx_ctx_t default_context = {0}; // Per connection context
    multicast_server_ctx_t global_ctx = {0}; // Application global context

    int sender_thread_ret = 0;
    picoquic_packet_loop_param_t sender_loop_params = {0};

    global_ctx.served_filename = served_file;
    global_ctx.server_cert = server_cert;
    global_ctx.server_key = server_key;
    global_ctx.sender_port = sender_port;
    global_ctx.client_threshold = client_threshold;
    global_ctx.is_local = is_local;
    global_ctx.quic = quic;
    global_ctx.sender_app_ctx.server_ctx = &global_ctx;

    global_ctx.sender_loop_params = &sender_loop_params;
    global_ctx.sender_thread_ret = &sender_thread_ret;

    default_context.global_ctx = &global_ctx;

    printf("Starting Picoquic Multicast server on port %d\n", server_port);
    printf("Serving file %s\n", served_file);

    /* Create the QUIC context for the server */
    current_time = picoquic_current_time();
    /* Create QUIC context */
    quic = picoquic_create(PICOQUIC_MULTICAST_MAX_CLIENTS, server_cert, server_key, NULL, PICOQUIC_MULTICAST_ALPN,
                           multicast_server_callback, &default_context, NULL, NULL, NULL, current_time, NULL, NULL, NULL, 0);

    if (quic == NULL)
    {
        fprintf(stderr, "Could not create server context\n");
        ret = -1;
    }
    else
    {
        picoquic_set_cookie_mode(quic, 2);
        picoquic_set_default_congestion_algorithm(quic, picoquic_bbr_algorithm);
        picoquic_set_qlog(quic, qlog_dir);
        picoquic_set_log_level(quic, 1);
        picoquic_enable_sslkeylog(quic, 1);
        picoquic_set_key_log_file_from_env(quic);
        
        // Always accept enable multicast
        picoquic_set_default_multicast_option(quic, 1);
        printf("Accept enable multicast: %s.\n", (quic->default_multicast_option) ? "Yes" : "No");

        struct sockaddr_storage group_ip;
        picoquic_store_text_addr(&group_ip, PICOQUIC_MULTICAST_GROUP_IP, PICOQUIC_MULTICAST_GROUP_PORT);

        picoquic_create_multicast_channel(quic, &global_ctx.mc_channel, PICOQUIC_MULTICAST_MAX_CLIENTS, &group_ip, NULL, max_rate);
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
    if (ret == 0)
    {
        ret = picoquic_packet_loop(quic, server_port, 0, 0, 0, 0, multicast_server_loop_cb, &global_ctx);
    }

    // And finish.
    printf("Server exit, ret = %d\n", ret);

    // Stop sender loop and delete sender context
    multicast_sender_ctx_t* sender_ctx = &global_ctx.sender_app_ctx;
    if (global_ctx.sender_running && sender_ctx != NULL) {
        multicast_server_delete_sender_context(sender_ctx);
    }

    // Clean up server context and global quic context
    if (quic != NULL) {
        picoquic_free(quic);
    }

    return ret;
}