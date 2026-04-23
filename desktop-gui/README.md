# Desktop GUI

This WinForms project provides a real desktop window for the current C++ backend.

Current capabilities:

1. Start and stop the receiver in background
2. Show live logs in the window
3. Handle pairing and incoming file confirmations through GUI message boxes
4. Run device discovery
5. Show local device info, a 6-digit connection code, and trusted paired devices
6. Remove stale paired-device records from the desktop UI when you no longer want to trust them
7. Choose a file or a folder and send it to a paired device that already has a known host and port
8. Folders are compressed to a zip file automatically before they are sent
