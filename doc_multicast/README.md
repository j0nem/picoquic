# picoquic Multicast

This repository implements an experimental multicast extension for the picoquic QUIC implementation.  
The implementation was written according to [draft-jholland-quic-multicast-08](https://datatracker.ietf.org/doc/html/draft-jholland-quic-multicast-08)

## Main features

- Multicast transport parameter support, negotiation for limits and supported algorithms
- Full multicast join/leave/retire state management with all necessary `MC_*` control frames
- IGMP/MLD Multicast Group join/leave via [`libmcrx`](https://github.com/GrumpyOldTroll/libmcrx)
- Integration of separate unidirectional multicast sender packet loop with Flow Control (rate limiting)
- Support for parallel reception of unicast and multicast traffic for clients via `select()` and `recvmsg()`
- Symmetric encryption and decryption of multicast traffic, including header protection via [`picotls`](https://github.com/h2o/picotls)
- Integrity hash generation for multicast frames, synchronization with unicast thread and transmission of `MC_INTEGRITY` frames to active clients via unicast
- Integrity hash verification client-side, including support for early or late `MC_INTEGRITY` frames by multicast packet caching
- Parital support for multicast acknowledgments via `MC_ACK`

## Limitations

The current implementation is experimental and some features are still missing. A rough overview of the limitations can be found here:  
[Current limitations](limitations.md)

## Setup

### Install Dependencies

Currently, this repository can only be used to build the multicast applications and the dependency for Multicast functionality `libmcrx` is always required.

The following dependencies should be available:

- [`picotls`](https://github.com/h2o/picotls) is used by picoquic to support TLS 1.3
- [`libmcrx`](https://github.com/GrumpyOldTroll/libmcrx) is used by the multicast extension for joining and leaving IP Multicast groups

`picotls` can either be installed system-wide or fetched during the `cmake` process by adding the option `-DPICOQUIC_FETCH_PTLS=Y` to the cmake command.

`libmcrx` has to be installed system-wide, while the library should stay in `/usr/lib` or `/usr/local/lib` and the include path should be either `/usr/include` `/usr/include/mcrx` or `/usr/local/include`

### Generate build files

Run `cmake` to generate the build files:

```bash
cmake -S <path-to-project-src> -B <build-target>
```

### Build sample applications

Currently, only the `multicast_sample` and the `datagram_sample` can be built using this repository.

Building the multicast sample:

```bash
make multicast
```

Building the datagram sample:
```bash
make dgramspl
```

## Sample applications

Refer to the README files for the usage of the sample applications:

- [Multicast sample usage](../multicast_sample/README.md)
- [Datagram sample usage](../datagram_sample/README.md)

Both applications use simple `DATAGRAM` frames, while the Multicast sample delivers them via unicast, the multicast sample delivers them via multicast and keeps a unicast connection to all clients, as specified in draft-jholland-quic-multicast-08.

The following very simple data structure is used in both applications in the `DATAGRAM` payload to deliver minimal metadata to the client application:

```
data fragment {
  reserved (6)  ⎤
  is_first (1)  ⎥ Byte 0
  is_last (1)   ⎦
  number (64)   ] Bytes 1-8
  data (...)    ] Bytes 9-end
}
```

## Multicast API

The picoquic API was extended to support some multicast features.  
See [Multicast API](multicast-api.md) for the reference.