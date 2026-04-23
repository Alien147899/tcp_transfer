package com.example.lantransfer.network

import com.example.lantransfer.model.DiscoveredDesktop
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException

class ConnectionCodeResolver {
    fun resolve(code: String, pairedDeviceIds: Set<String>): DiscoveredDesktop? {
        require(code.length == 6 && code.all { it.isDigit() }) { "connection code must be 6 digits" }

        DatagramSocket().use { socket ->
            socket.broadcast = true
            socket.reuseAddress = true
            socket.soTimeout = RECEIVE_TIMEOUT_MS

            val payload = "LFT_CODE_LOOKUP|1|$code".toByteArray(Charsets.UTF_8)
            for (target in getBroadcastTargets()) {
                val request = DatagramPacket(
                    payload,
                    payload.size,
                    target,
                    DISCOVERY_PORT,
                )
                socket.send(request)
            }

            val deadline = System.currentTimeMillis() + LOOKUP_WINDOW_MS
            val buffer = ByteArray(MAX_PACKET_SIZE)
            while (System.currentTimeMillis() < deadline) {
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    socket.receive(packet)
                    val payloadText = String(packet.data, 0, packet.length, Charsets.UTF_8)
                    return parseLookupResponse(
                        payload = payloadText,
                        expectedCode = code,
                        host = packet.address.hostAddress.orEmpty(),
                        pairedDeviceIds = pairedDeviceIds,
                    )
                } catch (_: SocketTimeoutException) {
                    continue
                } catch (_: IllegalArgumentException) {
                    continue
                }
            }
        }

        return null
    }

    private fun parseLookupResponse(
        payload: String,
        expectedCode: String,
        host: String,
        pairedDeviceIds: Set<String>,
    ): DiscoveredDesktop {
        val fields = splitFields(payload)
        require(fields.size == 6) { "invalid lookup response field count" }
        require(fields[0] == LOOKUP_PREFIX) { "invalid lookup response prefix" }
        require(fields[1] == LOOKUP_VERSION) { "unsupported lookup response version" }
        require(fields[2] == expectedCode) { "lookup response code mismatch" }

        val deviceId = unescapeField(fields[3])
        val deviceName = unescapeField(fields[4])
        val listenPort = fields[5].toInt().takeIf { it in 1..65535 }
            ?: throw IllegalArgumentException("invalid lookup response port")

        return DiscoveredDesktop(
            deviceId = deviceId,
            deviceName = deviceName,
            host = host,
            port = listenPort,
            paired = pairedDeviceIds.contains(deviceId),
        )
    }

    private fun splitFields(payload: String): List<String> {
        val fields = mutableListOf<String>()
        val current = StringBuilder()
        var escaping = false
        for (ch in payload) {
            if (escaping) {
                current.append(ch)
                escaping = false
                continue
            }
            when (ch) {
                '\\' -> escaping = true
                '|' -> {
                    fields += current.toString()
                    current.clear()
                }
                else -> current.append(ch)
            }
        }
        require(!escaping) { "lookup payload ends with dangling escape" }
        fields += current.toString()
        return fields
    }

    private fun unescapeField(value: String): String {
        val result = StringBuilder(value.length)
        var escaping = false
        for (ch in value) {
            if (escaping) {
                result.append(ch)
                escaping = false
                continue
            }
            if (ch == '\\') {
                escaping = true
            } else {
                result.append(ch)
            }
        }
        require(!escaping) { "lookup field ends with dangling escape" }
        return result.toString()
    }

    private companion object {
        private const val DISCOVERY_PORT = 38561
        private const val LOOKUP_PREFIX = "LFT_CODE_MATCH"
        private const val LOOKUP_VERSION = "1"
        private const val MAX_PACKET_SIZE = 1024
        private const val RECEIVE_TIMEOUT_MS = 500
        private const val LOOKUP_WINDOW_MS = 2500L
    }
}
