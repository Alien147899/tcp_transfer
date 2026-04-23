package com.example.lantransfer.network

import android.content.Context
import android.net.Uri
import android.os.Environment
import androidx.documentfile.provider.DocumentFile
import com.example.lantransfer.model.PairedDevice
import com.example.lantransfer.protocol.MessageType
import com.example.lantransfer.protocol.ProtocolCodec
import com.example.lantransfer.storage.DeviceStore
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicBoolean

class AndroidReceiverServer(
    private val context: Context,
    private val deviceStore: DeviceStore,
    private val codec: ProtocolCodec = ProtocolCodec(),
    private val onLog: (String) -> Unit,
    private val onStatusChanged: (Boolean, Int, String) -> Unit,
    private val onPairRequest: (String) -> Boolean,
    private val onFileRequest: (String, String, Long) -> Boolean,
) {
    private val running = AtomicBoolean(false)
    private var listenPort: Int = DEFAULT_PORT
    private var serverThread: Thread? = null
    private var presenceThread: Thread? = null
    private var codeLookupThread: Thread? = null
    private var serverSocket: ServerSocket? = null

    fun start(port: Int = DEFAULT_PORT) {
        if (!running.compareAndSet(false, true)) {
            onStatusChanged(true, listenPort, makeConnectionCode())
            return
        }

        listenPort = port

        serverThread = Thread({ runServerLoop() }, "android-receiver-server").apply { start() }
        presenceThread = Thread({ runPresenceLoop() }, "android-receiver-presence").apply { start() }
        codeLookupThread = Thread({ runCodeLookupLoop() }, "android-receiver-code").apply { start() }
    }

    fun stop() {
        if (!running.compareAndSet(true, false)) {
            return
        }
        runCatching { serverSocket?.close() }
        serverSocket = null
        val currentThread = Thread.currentThread()
        listOf(serverThread, presenceThread, codeLookupThread).forEach { thread ->
            if (thread != null && thread !== currentThread) {
                runCatching { thread.join(1200) }
            }
        }
        serverThread = null
        presenceThread = null
        codeLookupThread = null
        onStatusChanged(false, listenPort, makeConnectionCode())
    }

    fun isRunning(): Boolean = running.get()

    fun currentCode(): String = makeConnectionCode()

    private fun runServerLoop() {
        try {
            ServerSocket().use { socket ->
                serverSocket = socket
                socket.reuseAddress = true
                socket.soTimeout = 1000
                socket.bind(InetSocketAddress(listenPort))
                onStatusChanged(true, listenPort, makeConnectionCode())
                onLog("Phone receiver listening on port $listenPort")

                while (running.get()) {
                    try {
                        val client = socket.accept()
                        handleClient(client)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }
                }
            }
        } catch (ex: Exception) {
            if (running.get()) {
                onLog("Receiver stopped unexpectedly: ${ex.message}")
            }
        } finally {
            serverSocket = null
            if (running.get()) {
                stop()
            }
        }
    }

    private fun handleClient(socket: Socket) {
        socket.use { client ->
            client.soTimeout = IO_TIMEOUT_MS
            val peerHost = client.inetAddress?.hostAddress.orEmpty()
            val input = DataInputStream(BufferedInputStream(client.getInputStream()))
            val output = DataOutputStream(BufferedOutputStream(client.getOutputStream()))

            try {
                val initialFrame = codec.receiveFrame(input)
                when (initialFrame.type) {
                    MessageType.PairRequest -> handlePairRequest(initialFrame, output, input, peerHost)
                    MessageType.FileRequest -> handleFileRequest(initialFrame, output, input, peerHost)
                    MessageType.Error -> throw IllegalStateException(codec.parseError(initialFrame))
                    else -> throw IllegalStateException("Unexpected initial frame: ${initialFrame.type}")
                }
            } catch (ex: Exception) {
                runCatching {
                    codec.sendFrame(output, codec.buildError(ex.message ?: "request failed"))
                }
                onLog("Incoming request failed: ${ex.message}")
            }
        }
    }

    private fun handlePairRequest(
        initialFrame: com.example.lantransfer.protocol.MessageFrame,
        output: DataOutputStream,
        input: DataInputStream,
        peerHost: String,
    ) {
        val localDevice = deviceStore.getOrCreateLocalDevice()
        val requesterDeviceId = codec.parsePairRequest(initialFrame)
        if (!onPairRequest(requesterDeviceId)) {
            codec.sendFrame(output, codec.buildPairReject("pairing rejected by phone"))
            onLog("Rejected pair request from $requesterDeviceId")
            return
        }

        codec.sendFrame(output, codec.buildPairAccept(localDevice.deviceId))
        val finalFrame = codec.receiveFrame(input)
        when (finalFrame.type) {
            MessageType.PairReject -> throw IllegalStateException(codec.parseRejectMessage(finalFrame, MessageType.PairReject))
            MessageType.Error -> throw IllegalStateException(codec.parseError(finalFrame))
            MessageType.PairFinalize -> {
                val finalize = codec.parsePairFinalize(finalFrame)
                require(finalize.requesterDeviceId == requesterDeviceId) { "pair finalize requester mismatch" }
                require(finalize.accepterDeviceId == localDevice.deviceId) { "pair finalize accepter mismatch" }
                val existing = deviceStore.findPairedDevice(requesterDeviceId)
                deviceStore.upsertPairedDevice(
                    PairedDevice(
                        requesterDeviceId,
                        peerHost.ifBlank { existing?.host.orEmpty() },
                        existing?.port ?: 0,
                    )
                )
                onLog("Pairing completed with $requesterDeviceId")
            }
            else -> throw IllegalStateException("Expected pair finalize")
        }
    }

    private fun handleFileRequest(
        initialFrame: com.example.lantransfer.protocol.MessageFrame,
        output: DataOutputStream,
        input: DataInputStream,
        peerHost: String,
    ) {
        val localDevice = deviceStore.getOrCreateLocalDevice()
        val request = codec.parseFileRequest(initialFrame)
        require(request.recipientDeviceId == localDevice.deviceId) { "recipient device id mismatch" }
        require(request.fileSize > 0L) { "incoming file is empty" }
        require(request.fileSize <= MAX_INCOMING_FILE_BYTES) { "incoming file exceeds phone receiver limit" }

        val paired = deviceStore.findPairedDevice(request.senderDeviceId)
        require(paired != null) { "sender device is not paired" }

        if (!onFileRequest(request.senderDeviceId, request.fileName, request.fileSize)) {
            codec.sendFrame(output, codec.buildFileReject("file transfer rejected by phone"))
            onLog("Rejected file from ${request.senderDeviceId}")
            return
        }

        codec.sendFrame(output, codec.buildFileAccept(localDevice.deviceId))

        val safeName = sanitizeFileName(request.fileName)
        val target = createReceiveTarget(safeName)

        BufferedOutputStream(target.outputStream).use { fileOut ->
            var remaining = request.fileSize
            while (remaining > 0L) {
                val frame = codec.receiveFrame(input)
                require(frame.type == MessageType.FileChunk) { "expected file chunk" }
                require(frame.payload.size.toLong() <= remaining) { "received more data than declared" }
                fileOut.write(frame.payload)
                remaining -= frame.payload.size.toLong()
            }
        }

        val completion = codec.receiveFrame(input)
        require(completion.type == MessageType.TransferComplete) { "missing transfer complete frame" }

        target.commit()
        deviceStore.upsertPairedDevice(PairedDevice(request.senderDeviceId, peerHost, paired.port))
        onLog("Saved incoming file to ${target.displayPath}")
    }

    private fun runPresenceLoop() {
        val localDevice = deviceStore.getOrCreateLocalDevice()
        val payload = buildDiscoveryPayload(localDevice.deviceId, localDevice.deviceName, listenPort)
        val payloadBytes = payload.toByteArray(Charsets.UTF_8)
        try {
            DatagramSocket().use { socket ->
                socket.broadcast = true
                onLog("Phone presence broadcast started.")
                while (running.get()) {
                    for (target in getBroadcastTargets()) {
                        val packet = DatagramPacket(
                            payloadBytes,
                            payloadBytes.size,
                            target,
                            DISCOVERY_PORT,
                        )
                        socket.send(packet)
                    }
                    Thread.sleep(1000)
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun runCodeLookupLoop() {
        val localDevice = deviceStore.getOrCreateLocalDevice()
        val localCode = makeConnectionCode()
        try {
            DatagramSocket(null).use { socket ->
                socket.reuseAddress = true
                socket.soTimeout = 500
                socket.bind(InetSocketAddress(DISCOVERY_PORT))
                onLog("Phone code lookup responder started for code $localCode.")

                val buffer = ByteArray(MAX_PACKET_SIZE)
                while (running.get()) {
                    val packet = DatagramPacket(buffer, buffer.size)
                    try {
                        socket.receive(packet)
                        val payload = String(packet.data, 0, packet.length, Charsets.UTF_8)
                        val fields = splitEscapedFields(payload)
                        if (fields.size != 3 || fields[0] != "LFT_CODE_LOOKUP" || fields[1] != "1" || fields[2] != localCode) {
                            continue
                        }

                        val response = "LFT_CODE_MATCH|1|$localCode|${escapeField(localDevice.deviceId)}|${escapeField(localDevice.deviceName)}|$listenPort"
                        val bytes = response.toByteArray(Charsets.UTF_8)
                        val reply = DatagramPacket(bytes, bytes.size, packet.address, packet.port)
                        socket.send(reply)
                    } catch (_: SocketTimeoutException) {
                        continue
                    }
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun resolveDownloadDirectory(): File {
        return context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: File(context.filesDir, "downloads")
    }

    private fun createReceiveTarget(fileName: String): ReceiveTarget {
        val treeUriString = deviceStore.loadReceiveFolderUri()
        if (treeUriString.isNotBlank()) {
            val target = createTreeReceiveTarget(treeUriString, fileName)
            if (target != null) {
                return target
            }
            onLog("Selected receive folder is no longer writable. Falling back to app private downloads.")
        }

        val downloadDir = resolveDownloadDirectory()
        downloadDir.mkdirs()
        val outputFile = buildUniqueFile(downloadDir, fileName)
        val tempFile = File(outputFile.parentFile, "${outputFile.name}.part")
        if (tempFile.exists()) {
            tempFile.delete()
        }
        return ReceiveTarget(
            outputStream = tempFile.outputStream(),
            displayPath = outputFile.absolutePath,
            commitAction = {
                if (outputFile.exists()) {
                    outputFile.delete()
                }
                if (!tempFile.renameTo(outputFile)) {
                    throw IllegalStateException("failed to move temporary file into place")
                }
            },
        )
    }

    private fun createTreeReceiveTarget(treeUriString: String, fileName: String): ReceiveTarget? {
        val treeUri = Uri.parse(treeUriString)
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: return null
        if (!root.isDirectory || !root.canWrite()) {
            return null
        }

        val tempName = buildUniqueDocumentName(root, "$fileName.part")
        val tempDocument = root.createFile("application/octet-stream", tempName) ?: return null
        val outputStream = context.contentResolver.openOutputStream(tempDocument.uri, "w") ?: return null
        val finalName = buildUniqueDocumentName(root, fileName)

        return ReceiveTarget(
            outputStream = outputStream,
            displayPath = "${deviceStore.loadReceiveFolderLabel().ifBlank { root.name ?: "Selected folder" }}/$finalName",
            commitAction = {
                if (!tempDocument.renameTo(finalName)) {
                    throw IllegalStateException("failed to rename received document")
                }
            },
        )
    }

    private fun buildUniqueFile(directory: File, fileName: String): File {
        val dotIndex = fileName.lastIndexOf('.')
        val baseName = if (dotIndex > 0) fileName.substring(0, dotIndex) else fileName
        val extension = if (dotIndex > 0) fileName.substring(dotIndex) else ""
        var candidate = File(directory, fileName)
        var suffix = 2
        while (candidate.exists()) {
            candidate = File(directory, "$baseName ($suffix)$extension")
            suffix++
        }
        return candidate
    }

    private fun buildUniqueDocumentName(directory: DocumentFile, fileName: String): String {
        val dotIndex = fileName.lastIndexOf('.')
        val baseName = if (dotIndex > 0) fileName.substring(0, dotIndex) else fileName
        val extension = if (dotIndex > 0) fileName.substring(dotIndex) else ""
        var candidate = fileName
        var suffix = 2
        while (directory.findFile(candidate) != null) {
            candidate = "$baseName ($suffix)$extension"
            suffix++
        }
        return candidate
    }

    private fun sanitizeFileName(fileName: String): String {
        val fallback = fileName.ifBlank { "received-file" }
        return fallback.replace(Regex("[\\\\/:*?\"<>|]"), "_")
    }

    private fun buildDiscoveryPayload(deviceId: String, deviceName: String, listenPort: Int): String {
        return "LFT_DISCOVERY|1|${escapeField(deviceId)}|${escapeField(deviceName)}|$listenPort"
    }

    private fun escapeField(value: String): String {
        val builder = StringBuilder(value.length)
        value.forEach { ch ->
            if (ch == '\\' || ch == '|') {
                builder.append('\\')
            }
            builder.append(ch)
        }
        return builder.toString()
    }

    private fun splitEscapedFields(payload: String): List<String> {
        val fields = mutableListOf<String>()
        val current = StringBuilder()
        var escaping = false
        for (ch in payload) {
            if (escaping) {
                current.append(ch)
                escaping = false
                continue
            }
            if (ch == '\\') {
                escaping = true
            } else if (ch == '|') {
                fields += current.toString()
                current.clear()
            } else {
                current.append(ch)
            }
        }
        if (!escaping) {
            fields += current.toString()
        }
        return fields
    }

    private fun makeConnectionCode(): String {
        var hash = 2166136261u
        deviceStore.getOrCreateLocalDevice().deviceId.toByteArray(Charsets.UTF_8).forEach { byte ->
            hash = hash xor byte.toUByte().toUInt()
            hash *= 16777619u
        }
        val codeValue = 100000u + (hash % 900000u)
        return codeValue.toString().padStart(6, '0')
    }

    private companion object {
        private const val DEFAULT_PORT = 9000
        private const val DISCOVERY_PORT = 38561
        private const val MAX_PACKET_SIZE = 1024
        private const val IO_TIMEOUT_MS = 15000
        private const val MAX_INCOMING_FILE_BYTES = 8L * 1024L * 1024L * 1024L
    }

    private class ReceiveTarget(
        val outputStream: OutputStream,
        val displayPath: String,
        private val commitAction: () -> Unit,
    ) {
        fun commit() {
            commitAction()
        }
    }
}
