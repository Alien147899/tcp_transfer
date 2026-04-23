# Android Client

This Android Studio project provides a phone-side sender and receiver for the current LAN transfer protocol.

Current scope:

1. Generate and persist a local `device_id`
2. Start a phone receiver that listens on a TCP port, broadcasts presence, and responds to 6-digit code lookups
3. Scan the LAN for running receivers
4. Pair with another device by scan result, by 6-digit code, or by manual IP and port
5. Pick one file from Android storage
6. Send that file to a paired receiver
7. Accept one incoming file from a paired sender while the phone receiver is running
8. Remember the last target endpoint and target device ID
9. Show online receivers as quick-select and one-tap pairing entries in the phone UI
10. Show paired devices as quick-select entries in the phone UI
11. Display send progress and accept Android `ACTION_SEND` file shares

Current limitations:

1. The phone receiver currently runs only while the app is open
2. Files are received into the app-specific downloads directory
3. Pairs created from an incoming request may still need a later scan or code lookup before the phone knows the peer's listening port for sending back

Usage:

1. Open `android-app/` in Android Studio
2. Build and install the `app` module
3. On the phone, tap `Start receiver` if you want the phone to accept incoming pairs or files
4. To pair outwards, tap `Pair by Code`, `Scan LAN`, or manually enter host and port
5. After the first pairing, tap a paired device row to refill the form quickly
6. Pick a file and tap `Send file` to send it to the selected paired receiver
7. You can also share a file from another Android app directly into this app and then send it
