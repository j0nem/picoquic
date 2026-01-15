
# Multicast extended API methods and events

See `multicast_sample` for a complete example application workflow.

## General API

**Set Multicast option:**

```c
void picoquic_set_default_multicast_option(picoquic_quic_t* quic, int multicast_option)
```

**Find multicast channel subscription context in connection context:**

```c
picoquic_mc_channel_in_cnx_t *picoquic_find_multicast_channel_in_cnx(picoquic_multicast_channel_id_t *ch_id, picoquic_cnx_t *cnx)
```

**Set application callback for multicast data events (to send/receive multicast data):**

```c
void picoquic_set_callback_multicast(picoquic_multicast_channel_t *channel, picoquic_stream_data_mc_cb_fn callback_fn, void *callback_ctx)
```

**Start multicast sender loop:**

`picoquic_start_custom_network_thread_ex()` was extended, so that also a different packet loop function can be specified (multicast sender loop)

## Client API

**Set Multicast client parameters:**

```c
int picoquic_set_default_multicast_client_params(picoquic_quic_t *quic, picoquic_tp_multicast_client_params_t *params)
```

**Join Multicast channel:**

```c
int picoquic_join_mc_channel(picoquic_cnx_t *cnx, picoquic_multicast_channel_id_t *ch_id)
```

## Server API: 

**Create multicast channel**

```c
int picoquic_create_multicast_channel(picoquic_quic_t *quic, picoquic_multicast_channel_t **mc_channel, int max_clients, struct sockaddr_storage *group_ipv4, struct sockaddr_storage *group_ipv6, uint64_t max_rate)
```

**Schedule MC_ANNOUNCE, MC_JOIN and MC_KEY frames for a client:**

```c
int picoquic_schedule_mc_announce_and_join(picoquic_cnx_t *cnx, picoquic_multicast_channel_t *channel)
```

**Schedule MC_LEAVE and MC_RETIRE frames for a client (MC_RETIRE not yet implemented):**

```c
int picoquic_schedule_mc_leave_and_retire(picoquic_multicast_channel_t *channel)
```

**Schedule MC_LEAVE frame for a client:**

```c
int picoquic_schedule_mc_leave(picoquic_multicast_channel_t *channel, picoquic_cnx_t *cnx)
```

**Schedule MC_RETIRE frame for a client (not yet implemented):**

```c
int picoquic_schedule_mc_retire(picoquic_multicast_channel_t *channel, picoquic_cnx_t *cnx)
```

**Signal to Picoquic that the application wants to send `DATAGRAM` frames:**

```c
int picoquic_mark_datagram_ready_multicast(picoquic_multicast_channel_t* channel, int is_ready)
```

**Extension of picoquic_packet_loop_param_t for multicast sender:**

```c
typedef struct st_picoquic_packet_loop_param_t {
    ...
    // Only used with picoquic_packet_loop_multicast_send. If set, only send data for this channel in loop
    picoquic_multicast_channel_t* multicast_channel;
    int force_localhost_src_ip;
    ...
} picoquic_packet_loop_param_t;
```

## New application loop events (unicast callback):

**(Client) Can join a multicast channel:**

`fin_or_event` == `picoquic_callback_multicast_join_possible`  
`v_stream_ctx`: type `picoquic_multicast_channel_id_t *`

**(Server) Client attempted to join (MC_STATE(Joined) received):**

`fin_or_event` == `picoquic_callback_multicast_join_attempted`  
`v_stream_ctx`: type `picoquic_multicast_channel_id_t *`


**(Server) Client confirmed join (first MC_ACK received):**

`fin_or_event` == `picoquic_callback_multicast_join_confirmed`  
`v_stream_ctx`: type `picoquic_multicast_channel_id_t *`

**(Server) Client left the channel (sent MC_STATE(Left)):**

`fin_or_event` == `picoquic_callback_multicast_left`  
`v_stream_ctx`: type `picoquic_multicast_channel_id_t *`

**(Server) Client retired from the channel (sent MC_STATE(Retired)) (not yet implemented):**

`fin_or_event` == `picoquic_callback_multicast_retired`  
`v_stream_ctx`: type `picoquic_multicast_channel_id_t *`

## New application loop events (multicast callback):

**(Client) Received DATAGRAM frame over multicast:**

`fin_or_event` == `picoquic_callback_multicast_datagram`  

**(Client) Received STREAM frame over multicast (not yet implemented):**

`fin_or_event` == `picoquic_callback_multicast_stream_data`

**(Server) Prepare DATAGRAM frame to be sent via multicast:**

`fin_or_event` == `picoquic_callback_prepare_datagram`

**(Server) Prepare STREAM frame to be sent via multicast (not yet implemented):**

`fin_or_event` == `picoquic_callback_stream_data`

## Missing:

- Explicit leave API for client (schedule `MC_STATE(Left)`)
- API to schedule custom application `MC_STATE` frames
- API to schedule `MC_LIMITS` frame when limits changed 
