picoquic multicast
===============

The multicast program is a simple QUIC client/server multicast demo, opening a new multicast channel and delivering a specified file to all clients once a specified number of clients joined the multicast channel.

Building
--------
Run `make multicast` to build the multicast sample.

Usage
-----
Usage:

```bash
../multicast client server_name port folder max_rate
```

or :  

```bash
../multicast server port_server port_sender cert_file private_key_file max_rate served_file_name [client_threshold] [is_local]
```

The `max_rate` parameter (int) is the maximum rate in kibps according to the QUIC multicast draft specification.

The `client_threshold` parameter (int) specifies the number of clients needing to join the channel before the multicast sending process is started. 

The `is_local` parameter (string) can be set optionally (has to match the string `local` exactly) when all servers are running on localhost.
This forces the sender to use the "from IP" `127.0.0.1`.

Example
-------

Generate the certificates:

```bash
openssl req -x509 -newkey rsa:2048 -days 365 -keyout ca-key.pem -out ca-cert.pem
openssl req -newkey rsa:2048 -keyout server-key.pem -out server-req.pem
```

These commands will prompt a few questions, you don't need to put actual data
for this simple test.

Create a folder to hold server files:

```bash
mkdir server_files
echo "Hello world!" >> ./server_files/index.htm
```
And run the server:

```bash
./multicast server 4433 4434 ./ca-cert.pem ./server-key.pem 10240 /server_files/index.htm 2

```
Then, test if you can reach it using the client:

```bash
./multicast client localhost 4433 tmp/ 10240
```

Run another client to test multicast ability and trigger the multicast sender:
```bash
./multicast client localhost 4433 tmp2/ 10240
```

Getting logs
------------
Both server and clients will create logs of the connections if they can write files
in the expected folders. If you want logs, you will need to create these
folders before launching the server or the client.

The log files are in the [qlog format](https://datatracker.ietf.org/doc/draft-marx-qlog-event-definitions-quic-h3/).
They will be added to the working directory of client or server. The name of the log files are derived from
the initial connection identifier used for the connection, which is represented
as a string of hexadecimal digits. For example, if the Initial CID is
`012345678abcdef`, the logs created by client and server will be:

```
012345678abcdef.client.qlog
012345678abcdef.server.qlog
```

The qlog syntax is defined using JSON. The logs can be read using a text editor,
or with specialized tools like [QVIS](https://qvis.edm.uhasselt.be/)