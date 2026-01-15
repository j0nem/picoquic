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
- `MC_LIMITS` support
- Connection migration / Multipath for unicast connections not tested yet
- Congestion control
- Flow control
- Multicast logging
- Unit/Integrity tests

## Optional things not implemented:

- `MC_INTEGRITY` without length
- `MC_STATE` for application (custom)
- `MC_KEY` frame via multicast
- `MC_LEAVE` via multicast
- `MC_RETIRE` via multicast

## Improvement:

### Refine functionality:

- Remove "seat on callback"? - currently, sender thread is started (and more) within callback
- Leave/Retire
    - Review Leave/Retire timeout
    - Not always send both, Retire may be enough
- Rate limiting/Flow control enhancements in multicast channels
- Remove integrity hashes from context when not needed anymore
- Packet number encoding/decoding optimization
- Caching of packets when key arrives late
- Wake handling - active receiver condition
- Implement multicast key rotation
- Advanced limits check
- Consider received multicast packets in idle time calculation
- Datagram queueing for multicast
- Maybe also send `MC_ACK` in retransmitted packets
- Improve mcrx handling (do_receive callback)
- Maybe add support for spin bit

### Handle more cases:

- Error handling for edge cases (e.g. cnx closed unexpectedly)
- Error handling when decoding multicast frames
- State handling refinements
    - refine internal states ("active" states)
    - refine "join confirmed" state
    - Retire
- handling MC_LEAVE with wrong sequence number
- Handle RESET_STREAM frames

## Known Limitations:

(support may not be necessary)

- Channel cannot be joined in multiple connections
- Source address currently not configurable
- Channel id length not configurable
- Supported algorithms/max delay not configurable
- TIMESTAMP frame not supported (experimental draft)

## Application:

- GSO active/not active?
- Store data fragments in order
- Better channelLeave management on client
- Differentiation leave/retire

## Limitations of current specification:

- `MC_INTEGRITY` length field specifies number of hashes instead of total bytes  (only frame that needs context info to know the length)
- Usage of `DATAGRAMS` frames in multicast channels should be announced somewhere, similar to `max_datagram_frame_size`
- `max_ack_gap` currently based on unicast/static parameters, negotiaton needed?
- `MC_LEAVE` via multicast currently not possible due to needed sequence numbers
- Packet number compression currently not specified for multicast
- Key sequence numbers implicitly 0 before first frame
- Send `MC_INTEGRITY` before or after multicast data frames? --> before would minimize necessary buffer on client side
- Make sure that `MC_INTEGRITY` frames are not delayed too much
