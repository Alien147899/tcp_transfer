package com.example.lantransfer.network

import android.content.ContentResolver
import android.net.Uri
import com.example.lantransfer.model.PairedDevice
import com.example.lantransfer.protocol.MessageType
import com.example.lantransfer.protocol.ProtocolCodec
import java.io.BufferedInputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket

class DesktopTransferClient(
    private val contentResolver: ContentResolver,
    private val codec: ProtocolCodec = ProtocolCodec(),
) {
    fun pair(host: String, port: Int, localDeviceId: String): String {
        Socket().use { socket ->
            socket.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
            socket.soTimeout = IO_TIMEOUT_MS

            val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
            val output = DataOutputStream(socket.getOutputStream())

            codec.sendFrame(output, codec.buildPairRequest(localDeviceId))
            val response = codec.receiveFrame(input)
            when (response.type) {
                MessageType.PairReject -> throw IOException(
                    codec.parseRejectMessage(response, MessageType.PairReject)
                )

                MessageType.Error -> throw IOException(codec.parseError(response))
                MessageType.PairAccept -> {
                    val payload = codec.parsePairAccept(response)
                    codec.sendFrame(output, codec.buildPairFinalize(localDeviceId, payload.accepterDeviceId))
                    return payload.accepterDeviceId
                }

                else -> throw IOException("Unexpected pairing response: ${response.type}")
            }
        }
    }

    fun sendFile(
        host: String,
        port: Int,
        localDeviceId: String,
        pairedDevice: PairedDevice,
        uri: Uri,
        displayName: String,
        sizeBytes: Long,
        onProgress: (Long, Long) -> Unit = { _, _ -> },
    ) {
        if (sizeBytes <= 0L) {
            throw IOException("Selected file size is invalid")
        }

        Socket().use { socket ->
            socket.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
            socket.soTimeout = IO_TIMEOUT_MS

            val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
            val output = DataOutputStream(socket.getOutputStream())

            codec.sendFrame(
                output,
                codec.buildFileRequest(
                    senderDeviceId = localDeviceId,
                    recipientDeviceId = pairedDevice.deviceId,
                    fileName = displayName,
                    fileSize = sizeBytes,
                )
            )

            val response = codec.receiveFrame(input)
            when (response.type) {
                MessageType.FileReject -> throw IOException(
                    codec.parseRejectMessage(response, MessageType.FileReject)
                )

                MessageType.Error -> throw IOException(codec.parseError(response))
                MessageType.FileAccept -> {
                    val payload = codec.parseFileAccept(response)
                    if (payload.recipientDeviceId != pairedDevice.deviceId) {
                        throw IOException("File accept recipient does not match target device")
                    }
                }

                else -> throw IOException("Unexpected file request response: ${response.type}")
            }

            contentResolver.openInputStream(uri)?.use { stream ->
                val buffer = ByteArray(64 * 1024)
                var sentBytes = 0L
                while (true) {
                    val count = stream.read(buffer)
                    if (count < 0) {
                        break
                    }
                    if (count == 0) {
                        continue
                    }
                    codec.sendFrame(output, codec.buildFileChunk(buffer, count))
                    sentBytes += count.toLong()
                    onProgress(sentBytes, sizeBytes)
                }
            } ?: throw IOException("Unable to open selected file")

            codec.sendFrame(output, codec.buildTransferComplete())
        }
    }

    companion object {
        private const val CONNECT_TIMEOUT_MS = 5000
        private const val IO_TIMEOUT_MS = 15000
    }
}
