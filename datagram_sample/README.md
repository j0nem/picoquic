picoquic datagram sample
===============

The sample program is a very simple QUIC client/server demo transmitting `DATAGRAM` frames from the server to the client. This sample uses standard unicast to deliver the frames of a specified file, it was developed for comparison to to the [Multicast Sample](../multicast_sample/README.md).

Building
--------
Run `make dgramspl` to build the application.

Usage
-----
Usage:

```bash
../dgramspl client server_name port folder
```

or :  

```bash
../dgramspl server port cert_file private_key_file file_path
```

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
./dgramspl server 4433 ./ca-cert.pem ./server-key.pem ./server_files
```
Then, test if you can reach it using the client:

```bash
./dgramspl client localhost 4433 /tmp index.htm
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