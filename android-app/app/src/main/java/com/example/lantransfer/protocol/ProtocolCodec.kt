package com.example.lantransfer.protocol

import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.EOFException
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

private const val PROTOCOL_VERSION = 1

enum class MessageType(val wireValue: Int) {
    PairRequest(1),
    PairAccept(2),
    PairReject(3),
    PairFinalize(4),
    FileRequest(10),
    FileAccept(11),
    FileReject(12),
    FileChunk(13),
    TransferComplete(14),
    Error(255);

    companion object {
        fun fromWireValue(value: Int): MessageType {
            return entries.firstOrNull { it.wireValue == value }
                ?: throw IOException("Unknown message type: $value")
        }
    }
}

data class MessageFrame(
    val version: Int = PROTOCOL_VERSION,
    val type: MessageType,
    val payload: ByteArray = ByteArray(0),
)

data class PairAcceptPayload(val accepterDeviceId: String)
data class PairFinalizePayload(val requesterDeviceId: String, val accepterDeviceId: String)
data class FileAcceptPayload(val recipientDeviceId: String)
data class FileRequestPayload(
    val senderDeviceId: String,
    val recipientDeviceId: String,
    val fileName: String,
    val fileSize: Long,
)

class ProtocolCodec {
    fun sendFrame(output: DataOutputStream, frame: MessageFrame) {
        if (frame.payload.size > MAX_PAYLOAD_LENGTH) {
            throw IOException("Payload exceeds max frame size")
        }

        output.writeInt(MAGIC.toInt())
        output.writeByte(frame.version)
        output.writeByte(frame.type.wireValue)
        output.writeInt(frame.payload.size)
        if (frame.payload.isNotEmpty()) {
            output.write(frame.payload)
        }
        output.flush()
    }

    fun receiveFrame(input: DataInputStream): MessageFrame {
        val magic = try {
            input.readInt().toUInt()
        } catch (_: EOFException) {
            throw IOException("Connection closed")
        }
        if (magic != MAGIC) {
            throw IOException("Invalid frame magic")
        }

        val version = input.readUnsignedByte()
        if (version != VERSION) {
            throw IOException("Unsupported protocol version: $version")
        }

        val type = MessageType.fromWireValue(input.readUnsignedByte())
        val payloadLength = input.readInt()
        if (payloadLength < 0 || payloadLength > MAX_PAYLOAD_LENGTH) {
            throw IOException("Invalid payload length: $payloadLength")
        }

        val payload = ByteArray(payloadLength)
        input.readFully(payload)
        return MessageFrame(version = version, type = type, payload = payload)
    }

    fun buildPairRequest(deviceId: String): MessageFrame {
        return MessageFrame(type = MessageType.PairRequest, payload = encodeStrings(deviceId))
    }

    fun parsePairRequest(frame: MessageFrame): String {
        ensureType(frame, MessageType.PairRequest)
        return decodeStrings(frame.payload, 1)[0]
    }

    fun parsePairAccept(frame: MessageFrame): PairAcceptPayload {
        ensureType(frame, MessageType.PairAccept)
        val fields = decodeStrings(frame.payload, 1)
        return PairAcceptPayload(fields[0])
    }

    fun buildPairFinalize(requesterDeviceId: String, accepterDeviceId: String): MessageFrame {
        return MessageFrame(
            type = MessageType.PairFinalize,
            payload = encodeStrings(requesterDeviceId, accepterDeviceId),
        )
    }

    fun parsePairFinalize(frame: MessageFrame): PairFinalizePayload {
        ensureType(frame, MessageType.PairFinalize)
        val fields = decodeStrings(frame.payload, 2)
        return PairFinalizePayload(fields[0], fields[1])
    }

    fun buildPairAccept(accepterDeviceId: String): MessageFrame {
        return MessageFrame(type = MessageType.PairAccept, payload = encodeStrings(accepterDeviceId))
    }

    fun buildPairReject(reason: String): MessageFrame {
        return MessageFrame(type = MessageType.PairReject, payload = encodeStrings(reason))
    }

    fun parseRejectMessage(frame: MessageFrame, expectedType: MessageType): String {
        ensureType(frame, expectedType)
        return decodeStrings(frame.payload, 1)[0]
    }

    fun parseError(frame: MessageFrame): String {
        ensureType(frame, MessageType.Error)
        return String(frame.payload, Charsets.UTF_8)
    }

    fun buildFileRequest(
        senderDeviceId: String,
        recipientDeviceId: String,
        fileName: String,
        fileSize: Long,
    ): MessageFrame {
        val nameBytes = fileName.toByteArray(Charsets.UTF_8)
        val senderBytes = senderDeviceId.toByteArray(Charsets.UTF_8)
        val recipientBytes = recipientDeviceId.toByteArray(Charsets.UTF_8)
        val payload = ByteBuffer.allocate(
            4 + senderBytes.size +
                4 + recipientBytes.size +
                4 + nameBytes.size +
                8
        ).order(ByteOrder.BIG_ENDIAN)
        payload.putInt(senderBytes.size)
        payload.put(senderBytes)
        payload.putInt(recipientBytes.size)
        payload.put(recipientBytes)
        payload.putInt(nameBytes.size)
        payload.put(nameBytes)
        payload.putLong(fileSize)
        return MessageFrame(type = MessageType.FileRequest, payload = payload.array())
    }

    fun parseFileAccept(frame: MessageFrame): FileAcceptPayload {
        ensureType(frame, MessageType.FileAccept)
        val fields = decodeStrings(frame.payload, 1)
        return FileAcceptPayload(fields[0])
    }

    fun parseFileRequest(frame: MessageFrame): FileRequestPayload {
        ensureType(frame, MessageType.FileRequest)
        val buffer = ByteBuffer.wrap(frame.payload).order(ByteOrder.BIG_ENDIAN)
        val senderDeviceId = readStringField(buffer, "sender_device_id")
        val recipientDeviceId = readStringField(buffer, "recipient_device_id")
        val fileName = readStringField(buffer, "file_name")
        if (buffer.remaining() != 8) {
            throw IOException("Invalid trailing payload in file request")
        }
        val fileSize = buffer.long
        return FileRequestPayload(senderDeviceId, recipientDeviceId, fileName, fileSize)
    }

    fun buildFileAccept(recipientDeviceId: String): MessageFrame {
        return MessageFrame(type = MessageType.FileAccept, payload = encodeStrings(recipientDeviceId))
    }

    fun buildFileReject(reason: String): MessageFrame {
        return MessageFrame(type = MessageType.FileReject, payload = encodeStrings(reason))
    }

    fun buildFileChunk(chunk: ByteArray, count: Int): MessageFrame {
        if (count > MAX_PAYLOAD_LENGTH) {
            throw IOException("Chunk too large")
        }
        return MessageFrame(type = MessageType.FileChunk, payload = chunk.copyOf(count))
    }

    fun buildTransferComplete(): MessageFrame {
        return MessageFrame(type = MessageType.TransferComplete)
    }

    fun buildError(message: String): MessageFrame {
        return MessageFrame(type = MessageType.Error, payload = message.toByteArray(Charsets.UTF_8))
    }

    private fun encodeStrings(vararg values: String): ByteArray {
        val encoded = values.map { it.toByteArray(Charsets.UTF_8) }
        val totalSize = encoded.sumOf { 4 + it.size }
        val buffer = ByteBuffer.allocate(totalSize).order(ByteOrder.BIG_ENDIAN)
        encoded.forEach { item ->
            buffer.putInt(item.size)
            buffer.put(item)
        }
        return buffer.array()
    }

    private fun decodeStrings(payload: ByteArray, expectedCount: Int): List<String> {
        val buffer = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN)
        val result = ArrayList<String>(expectedCount)
        repeat(expectedCount) {
            if (buffer.remaining() < 4) {
                throw IOException("Missing string length")
            }
            val size = buffer.int
            if (size <= 0 || buffer.remaining() < size) {
                throw IOException("Invalid string field")
            }
            val bytes = ByteArray(size)
            buffer.get(bytes)
            result += String(bytes, Charsets.UTF_8)
        }
        if (buffer.hasRemaining()) {
            throw IOException("Unexpected trailing payload")
        }
        return result
    }

    private fun readStringField(buffer: ByteBuffer, fieldName: String): String {
        if (buffer.remaining() < 4) {
            throw IOException("Missing length for $fieldName")
        }
        val size = buffer.int
        if (size <= 0 || buffer.remaining() < size) {
            throw IOException("Invalid size for $fieldName")
        }
        val bytes = ByteArray(size)
        buffer.get(bytes)
        return String(bytes, Charsets.UTF_8)
    }

    private fun ensureType(frame: MessageFrame, type: MessageType) {
        if (frame.type != type) {
            throw IOException("Expected $type but received ${frame.type}")
        }
    }

    companion object {
        const val VERSION = PROTOCOL_VERSION
        const val MAX_PAYLOAD_LENGTH = 1024 * 1024
        val MAGIC: UInt = 0x4C465431u
    }
}
