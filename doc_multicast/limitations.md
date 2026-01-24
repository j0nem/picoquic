# Limitations of Picoquic Multicast

The following work-in-progress comments are used in the code to highlight relevant limitations and possibilities for improvements:

- `TODO MC`
- `ENHANCE MC`
- `CHECK MC`
- `CLEAN MC`

## Main features not implemented:

- IPv6 support (mainly test, high-level config additions)
- `STREAM` frames in multicast channels + retransmissions
- multicast acknowlegement:
    - `MC_ACK` decoding and handling + ack of MC_ACK management
    - `MC_ACK` handling - condiering `max_ack_gap` of `ACK_FREQUENCY`?
- `MC_LIMITS` support (changing limits of client)
- Connection migration / Multipath for unicast connections not tested yet
- Congestion control (leaving channels when limit is exeeded)
- Multicast logging
- Unit/Integrity tests

## Additional features not implemented:

- `MC_INTEGRITY` without length
- `MC_STATE` for application (custom)
- `MC_KEY` frame via multicast
- `MC_LEAVE` via multicast
- `MC_RETIRE` via multicast

## Improvement:

### Refine functionality:

- Improve scalability and test with more clients
- Review Leave/Retire timeout
- Rate limiting/Flow control enhancements in multicast channels
- Cleanup integrity hashes from context when not needed anymore
- Packet number encoding/decoding optimization
- Caching of packets when key arrives late
- Wake handling - active receiver condition
- Implement multicast key rotation
- Advanced limits check
- Consider received multicast packets in idle time calculation
- Support Datagram queueing API for multicast (currently, only just-in-time API is supported)
- Maybe also send `MC_ACK` in retransmitted packets
- Review mcrx handling (do_receive callback)
- Review spin bit

### Handle more cases:

- Error handling for edge cases (e.g. cnx closed unexpectedly)
- Error handling when decoding multicast frames
- State handling refinements
    - refine internal states ("active" states)
    - refine "join confirmed" state
    - Retire
- handling MC_LEAVE with wrong sequence number
- Handle RESET_STREAM frames

## Application:

- Data fragments should be orderd at application level
- Better channel retire management on server

## Limitations of current specification:

- `MC_INTEGRITY` length field specifies number of hashes instead of total bytes  (only frame that needs context info to know the length)
- Usage of `DATAGRAMS` frames in multicast channels should be announced somewhere, similar to `max_datagram_frame_size`
- `max_ack_gap` currently based on unicast/static parameters, negotiaton needed?
- `MC_LEAVE` via multicast currently not possible due to needed sequence numbers
- Packet number compression currently not specified for multicast
- Key sequence numbers implicitly 0 before first frame
- Send `MC_INTEGRITY` before or after multicast data frames? --> before would minimize necessary buffer on client side
- Make sure that `MC_INTEGRITY` frames are not delayed too much
