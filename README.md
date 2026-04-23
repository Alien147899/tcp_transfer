# LAN File Transfer

Minimal phase-1 LAN file transfer tool built with C++17 and CMake.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

Receiver:

```bash
./lan_transfer receive 9000 ./received_files
```

Discovery:

```bash
./lan_transfer discover 9000 5
```

Pairing initiator:

```bash
./lan_transfer pair 192.168.1.10 9000
```

Sender:

```bash
./lan_transfer send 192.168.1.10 9000 <target_device_id> ./example.txt
```

Protocol frame format:

1. `4 bytes magic`
2. `1 byte version`
3. `1 byte type`
4. `4 bytes payload_length`
5. `payload`

Current message flow:

1. `FileRequest` frame
2. `FileAccept` or `FileReject`
3. One or more `FileChunk` frames
4. `TransferComplete` frame

Pairing message flow:

1. `PairRequest`
2. `PairAccept` or `PairReject`
3. `PairFinalize`

Persistent files:

1. `device_info.json` stores the local `device_id` and `device_name`
2. `paired_devices.json` stores successfully paired devices
3. `settings.json` can override selected runtime limits such as `max_file_size_bytes`

UDP discovery:

1. Periodically broadcasts `device_id`, `device_name`, and TCP `listen_port`
2. Maintains an online device list during the discovery session
3. Marks each discovered device as `paired` or `unpaired`
4. Discovery does not bypass pairing; unpaired devices still cannot send files
5. A running desktop receiver now also broadcasts its presence, so Android clients can scan for it without typing an IP address

Security behavior:

1. Receiver writes files only inside the configured download directory
2. Incoming file names are sanitized and path traversal is rejected
3. Incoming files are written to `*.part` first, then renamed on success
4. Default single-file size limit is `8 GiB`, configurable via `settings.json`
5. Incoming pair requests and file requests are rate limited
6. Blacklisted devices or hosts are rejected before pairing or file transfer

Optional `blacklist.json` format:

```json
{
  "device_ids": ["dev-badactor"],
  "hosts": ["192.168.1.55"]
}
```

Android client:

1. Android Studio project is in `android-app/`
2. The Android app now includes a phone receiver mode with its own 6-digit connection code and listen port
3. Android can pair by 6-digit code instead of requiring manual IP entry, and it can also scan the LAN for online receivers
4. Android can send one file to a paired receiver and can also receive incoming paired transfers while its receiver is running
5. Files can be picked inside the app or shared into it from other Android apps
6. It does not bypass pairing, file confirmation, or receiver-side security checks

Windows launcher:

1. Double-click `Launch GUI.bat`
2. Use the GUI to start the receiver, discover devices, open the download directory, and inspect paired devices
3. This avoids the command-line window flashing closed when the executable is started without arguments
