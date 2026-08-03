## How to create req.bin from a PCAP

Important: do not use the whole packet frame. Extract the application payload only.

### Option 1: HTTP payload from TCP stream using tshark
1. Find stream index:
```
tshark -r capture.pcap -Y http.request -T fields -e tcp.stream | head
```

2. Export one stream payload as raw hex and convert to binary:
```
tshark -r capture.pcap -qz follow,tcp,raw,<STREAM_ID> | tail -n +7 | tr -d '\n\r\t ' | xxd -r -p > req.bin
```

3. Run:
```
./build/Debug/flowforge --plugin plugin_http.so --input req.bin
```

### Option 2: DNS payload (UDP data only)
```
tshark -r capture.pcap -Y "dns && udp" -T fields -e udp.payload | head -n 1 | tr -d ':' | xxd -r -p > query.bin
./build/Debug/flowforge --plugin plugin_dns.so --input query.bin
```

### Option 3: TLS record payload from TCP stream
Use follow,tcp,raw as in HTTP, but select a stream that starts at a TLS record. plugin_tls_ja3 expects buffer[0] = 0x16 and handshake type ClientHello at offset 5:
- plugin_tls_ja3.cpp
- plugin_tls_ja3.cpp

### Gotcha with larger files

The host reads up to 65535 bytes per dispatch chunk:
- main.cpp

If your input file contains multiple packets/messages concatenated, each chunk is treated as one buffer passed to every plugin. For reliable single-message testing, create req.bin from one extracted payload (one HTTP message, one DNS datagram, or one TLS record).
