
/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
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

#include "picoquic.h"
#include "picoquic_internal.h"
#include "picoquic_utils.h"
#include "tls_api.h"
#include <stdlib.h>
#include <string.h>
#include <mcrx/libmcrx.h>
#ifndef _WINDOWS
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#endif

/* Call custom do_receive callback function OR mcrx_ctx_receive_packets to receive packets */
// TODO MC: is this function actually needed? Probably only if mcrx_ctx_set_receive_socket_handlers method does not work
int picoquic_mcrx_receive_packets(struct mcrx_ctx *ctx, 
    int (*do_receive)(intptr_t handle, int fd), 
    intptr_t* do_handle_val,
    int fd) 
{
    if (ctx == NULL) {
        return -1;
    }

    int ret;
    if (do_receive != NULL)  {
        ret = (*do_receive)(*do_handle_val, fd);
    } else {
        ret = mcrx_ctx_receive_packets(ctx);
    }

    if (ret != MCRX_ERR_OK &&
        ret != MCRX_ERR_NOTHING_JOINED &&
        ret != MCRX_ERR_TIMEDOUT) {
        return -1;
    }

    return 0;
}

/* Callback called by mcrx when socket was added */
int picoquic_mcrx_added_socket_cb(struct mcrx_ctx* ctx,
    intptr_t handle,
    int fd,
    int (*do_receive)(intptr_t handle, int fd)) 
{
    // TODO MC: Add socket to picoquic_quic context, so that it is included in picoquic_packet_loop_select's select() statement

    // do_receive call maybe not needed?
    // TODO MC: Figure out how to use the do_receive callback
    do_receive(handle, fd);
    return MCRX_ERR_OK;
}

/* Callback called by mcrx when socket was removed */
int picoquic_mcrx_removed_socket_cb(
    struct mcrx_ctx* ctx,
    int fd)
{
    // TODO MC: Remove socket to picoquic_quic context, so that it is excluded from picoquic_packet_loop_select's select() statement

    return MCRX_ERR_OK;
}

/* Initialize MCRX context and set receive socket handlers */
int picoquic_mcrx_initialize(struct mcrx_ctx **ctxp) 
{
    if (*ctxp != NULL) {
        fprintf(stdout, "Error in picoquic_mcrx_initialize: ctx already created\n");
        return -1;
    }
    int err = mcrx_ctx_new(ctxp);
    if (err != 0)  {
        fprintf(stdout, "Error in picoquic_mcrx_initialize: ctx object could not be created\n");
        return -1;
    }

    struct mcrx_ctx *ctx = *ctxp;

    mcrx_ctx_set_log_priority(ctx, MCRX_LOGLEVEL_WARNING);
    
    
    err = mcrx_ctx_set_receive_socket_handlers(ctx,
        picoquic_mcrx_added_socket_cb, picoquic_mcrx_removed_socket_cb);

    // TODO MC: Maybe add picoquic_quic context to mcrx_ctx userdata for socket add/remove callbacks
        
    if (err != 0) {
        ctx = mcrx_ctx_unref(ctx);
        fprintf(stdout, "Error in picoquic_mcrx_initialize: mcrx_ctx_set_receive_socket_handlers returned error\n");
        return err;
    }

    return 0;
}

/* Callback called by mcrx to handle received packets */
int picoquic_mcrx_receive_cb(struct mcrx_packet* pkt) {
    struct mcrx_subscription* sub = mcrx_packet_get_subscription(pkt);
    picoquic_mcrx_sub_info* info = (picoquic_mcrx_sub_info*)mcrx_subscription_get_userdata(sub);

    info->nb_packets++;
    uint8_t* data = 0;
    int len = mcrx_packet_get_contents(pkt, &data);

    // TODO MC: Figure out how to use this callback (if it is actually called in our setup), maybe call something like `picoquic_incoming_packet_ex` here

    return MCRX_RECEIVE_CONTINUE;
}

/* Join the channel via mcrx */
int picoquic_mcrx_join(struct mcrx_ctx **ctxp, picoquic_mc_channel_in_cnx_t *ch_in_cnx) 
{
    struct mcrx_ctx *ctx = *ctxp;

    if (ctx == NULL) {
        fprintf(stdout, "Error in picoquic_mcrx_join: ctx is NULL\n");
        return -1;
    }

    char src_ip[128 + 5];
    struct sockaddr* source = (struct sockaddr*) &ch_in_cnx->channel->source_ip;
    picoquic_addr_text(source, src_ip, sizeof(src_ip));
    strcpy(src_ip, strtok(src_ip, ":"));

    char group_ip[128 + 5];
    struct sockaddr* group = (struct sockaddr*) &ch_in_cnx->channel->group_ip;
    picoquic_addr_text(group, group_ip, sizeof(group_ip));
    strcpy(group_ip, strtok(group_ip, ":"));

    int err = 0;
    struct mcrx_subscription_config cfg = MCRX_SUBSCRIPTION_CONFIG_INIT;
    err = mcrx_subscription_config_pton(&cfg, src_ip, group_ip);

    if (err != 0) {
        fprintf(stdout, "Error in picoquic_mcrx_join while configuring mcrx_subscription: %i\n", err);
        
        return -1;
    }

    uint16_t port;
    if (group->sa_family == AF_INET) {
        port = ((struct sockaddr_in*)group)->sin_port;
    } else {
        port = ((struct sockaddr_in6*)group)->sin6_port;
    }

    cfg.port = port;

    struct mcrx_subscription* sub = 0;
    err = mcrx_subscription_new(ctx, &cfg, &sub);
    if (err != 0) {
        fprintf(stdout, "Error in picoquic_mcrx_join: subscription object could not be established\n");
        return -1;
    }

    picoquic_mcrx_sub_info* subinfo = (picoquic_mcrx_sub_info*)calloc(sizeof(picoquic_mcrx_sub_info), 1);
    if (!subinfo) {
        mcrx_subscription_unref(sub);
        fprintf(stdout, "Error picoquic_mcrx_join: subinfo could not be alloced\n");
        return -1;
    }

    subinfo->ch_in_cnx = ch_in_cnx;

    mcrx_subscription_set_receive_cb(sub, picoquic_mcrx_receive_cb);
    mcrx_subscription_set_userdata(sub, (intptr_t)subinfo);

    ch_in_cnx->mcrx_subscription = sub;

    err = mcrx_subscription_join(sub);
    if (err != 0) {
        mcrx_subscription_unref(sub);
        free(subinfo);
        fprintf(stdout, "Error in picoquic_mcrx_join: could not join with subscription object\n");
        return -1;
    }

    return 0;
}

/* Unref mcrx context object (and more, if necessary) */
int picoquic_mcrx_cleanup(struct mcrx_ctx **ctxp) {
    struct mcrx_ctx *ctx = *ctxp;
    if (ctx == NULL) {
        return -1;
    }
    mcrx_ctx_unref(ctx);
    return 0;
}

/* Leave channel via mcrx */
int picoquic_mcrx_leave(struct mcrx_subscription* sub) {
    picoquic_mcrx_sub_info* info = (picoquic_mcrx_sub_info*)mcrx_subscription_get_userdata(sub);
    mcrx_subscription_set_userdata(sub, 0);

    if (info) {
        free(info);
    }

    int err = mcrx_subscription_leave(sub);
    if (err != 0) {
        return -1;
    }

    sub = mcrx_subscription_unref(sub);
    return 0;
}