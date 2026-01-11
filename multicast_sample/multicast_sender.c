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
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <autoqlog.h>
#include <picoquic_packet_loop.h>
#include "picoquic_multicast.h"

/* Create the application datagram context */
static int multicast_sender_mark_datagram_ready(multicast_sender_ctx_t* sender_ctx)
{
    /* Mark the stream as active. The callback will be asked to provide data when 
    * the connection is ready. */
    int ret = picoquic_mark_datagram_ready_multicast(sender_ctx->server_ctx->mc_channel, 1);
    if (ret != 0) {
        fprintf(stdout, "Error %d, cannot mark datagram ready\n", ret);
    }

    return ret;
}

/* Open the file to be served via multicast */
int multicast_sender_open_file(multicast_sender_ctx_t* sender_ctx)
{
    int ret = 0;
    char file_path[1024];

    /* Verify the name, then try to open the file */
    if (sizeof(sender_ctx->server_ctx->served_filename) + 1 > sizeof(file_path)) {
        ret = PICOQUIC_MULTICAST_NAME_TOO_LONG_ERROR;
    }
    else {
        /* Use the picoquic_file_open API for portability to Windows and Linux */
        sender_ctx->F = picoquic_file_open(sender_ctx->server_ctx->served_filename, "rb");
        sender_ctx->file_was_opened = 1;

        if (sender_ctx->F == NULL) {
            ret = PICOQUIC_MULTICAST_NO_SUCH_FILE_ERROR;
        }
        else {
            /* Assess the file size, as this is useful for data planning */
            long sz;
            fseek(sender_ctx->F, 0, SEEK_END);
            sz = ftell(sender_ctx->F);

            if (sz <= 0) {
                sender_ctx->F = picoquic_file_close(sender_ctx->F);
                ret = PICOQUIC_MULTICAST_FILE_READ_ERROR;
            }
            else {
                sender_ctx->file_length = (size_t)sz;
                fseek(sender_ctx->F, 0, SEEK_SET);
                ret = 0;
            }
        }
    }

    return ret;
}

/* Schedule MC_LEAVE and MC_RETIRE frame to all clients (if not already done) to retire the channel */
void multicast_sender_retire(multicast_sender_ctx_t* sender_ctx) {
    picoquic_schedule_mc_leave_and_retire(sender_ctx->server_ctx->mc_channel);
}

/* Multicast application callback */
int multicast_sender_callback(picoquic_multicast_channel_t* channel,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    multicast_sender_ctx_t* sender_ctx = (multicast_sender_ctx_t*)callback_ctx;

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
        case picoquic_callback_application_close:
        case picoquic_callback_stream_gap:
        case picoquic_callback_almost_ready:
        case picoquic_callback_ready:
        case picoquic_callback_close: 
            // None of these events should happen, because we cannot receive anything
            // and do not handle any unicast connections
            break;
        case picoquic_callback_prepare_to_send:
            // Currently, no STREAM frames are sent, just DATAGRAMs - should not happen
            break;
        case picoquic_callback_prepare_datagram:

            // File sending finished, stop sender loop and delete 
            // thread at this point and notify server via callback
            if (sender_ctx->file_sent >= sender_ctx->file_length && sender_ctx->file_was_opened) {
                picoquic_mark_datagram_ready_multicast(sender_ctx->server_ctx->mc_channel, 0);
                multicast_sender_retire(sender_ctx);
                break;
            }

            if (!sender_ctx->file_was_opened && sender_ctx->F == NULL) {
                ret = multicast_sender_open_file(sender_ctx);
                if (ret != 0) {
                    fprintf(stderr, "Error while opening the requested file: %i\n", ret);
                    break;
                }
            }

            // Use header to indicate fragment number and first/last fragment
            int is_first = 0;
            int is_last = 0;
            int header_length = PICOQUIC_MULTICAST_DATAGRAM_HEADER_LENGTH;

            /* Implement the zero copy callback */
            size_t available = sender_ctx->file_length - sender_ctx->file_sent + header_length;
            int more_data = 0;
            uint8_t *buffer;

            if (sender_ctx->file_sent == 0 && sender_ctx->current_fragment_number == 0) {
                is_first = 1;
            } else {
                sender_ctx->current_fragment_number++;
            }
            
            if (available > length) {
                available = length;
                more_data = 1;
            } else {
                is_last = 1;
            }

            if (available <= PICOQUIC_MULTICAST_DATAGRAM_HEADER_LENGTH) {
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
                    memcpy(buffer + 1, &(sender_ctx->current_fragment_number), PICOQUIC_MULTICAST_DATAGRAM_HEADER_LENGTH - 1);
                    start_fread += header_length;
                    available -= header_length;

                    if (is_last == 1 && is_first != 1) {
                        fprintf(stdout, "is_last sent: First byte = 0x%x\n", buffer[0]);
                    } else if (is_last != 1 && is_first == 1) {
                        fprintf(stdout, "is_first sent: First byte = 0x%x\n", buffer[0]);
                    } else if (is_last == 1 && is_first == 1) {
                        fprintf(stdout, "is_first and is_last sent: First byte = 0x%x\n", buffer[0]);
                    }

                    size_t nb_read = fread(start_fread, 1, available, sender_ctx->F);

                    if (nb_read != available) {
                        /* Error while reading the file */
                        ret = -1;           
                    }
                    else {
                        sender_ctx->file_sent += nb_read;
                    }
                }
                else {
                    /* Should never happen according to callback spec. */
                    ret = -1;
                }

                // File sending finished, stop sender loop and delete 
                // thread at this point and notify server via callback
                if (!more_data) {
                    multicast_sender_retire(sender_ctx);
                }
            }
            break;
        default:
            /* unexpected -- just ignore. */
            break;
        }
    }

    return ret;
}

/* Prepare the context used by the multicast sender client and set callback */
static int multicast_sender_init(multicast_sender_ctx_t *sender_ctx,
    picoquic_multicast_channel_t* channel)
{
    int ret = 0;

    char text1[256];
    printf("Prepare multicast sending for group ip %s\n", 
        picoquic_addr_text((struct sockaddr*)&channel->group_ip, text1, sizeof(text1))
    );

    /* Set the callback context */
    picoquic_set_callback_multicast(channel, multicast_sender_callback, sender_ctx);

    return ret;
}

/* Start multicast sender server */
int multicast_sender_start(multicast_sender_ctx_t* sender_ctx) {
    int ret = 0;
    int thread_ret = 0;
    picoquic_packet_loop_param_t param = { 0 };
    multicast_server_ctx_t* server_ctx = sender_ctx->server_ctx;

    ret = multicast_sender_init(sender_ctx, server_ctx->mc_channel);

    /* Set the params for this thread */
    param.multicast_channel = server_ctx->mc_channel;
    param.local_port = server_ctx->sender_port;
    param.force_localhost_src_ip = 1; /* Force localhost as src ip for multicast packets for local tests */

    // CHECK MC: GSO deactivated currently due to issues with local interfaces, may be re-activated later?
    param.do_not_use_gso = 1;

    /* Start the background thread. */
    sender_ctx->thread_ctx = picoquic_start_custom_network_thread_ex(server_ctx->quic, &param,
        picoquic_internal_thread_create, picoquic_internal_thread_delete,
        picoquic_internal_thread_setname, "multicast_sender", 
        picoquic_packet_loop_multicast_send,
        NULL, NULL, &thread_ret);

    ret = multicast_sender_mark_datagram_ready(sender_ctx);

    return ret;
}