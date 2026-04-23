package com.example.lantransfer.network

import com.example.lantransfer.model.DiscoveredDesktop
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketException

class DesktopDiscoveryScanner {
    fun scan(durationMs: Long, pairedDeviceIds: Set<String>): List<DiscoveredDesktop> {
        val socket = DatagramSocket(DISCOVERY_PORT).apply {
            reuseAddress = true
            soTimeout = RECEIVE_TIMEOUT_MS
            broadcast = true
        }

        socket.use { datagramSocket ->
            val deadline = System.currentTimeMillis() + durationMs.coerceAtLeast(RECEIVE_TIMEOUT_MS.toLong())
            val devices = linkedMapOf<String, DiscoveredDesktop>()
            val buffer = ByteArray(MAX_PACKET_SIZE)

            while (System.currentTimeMillis() < deadline) {
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    datagramSocket.receive(packet)
                    val payload = String(packet.data, 0, packet.length, Charsets.UTF_8)
                    val parsed = parseDiscoveryPayload(
                        payload = payload,
                        host = packet.address.hostAddress.orEmpty(),
                        pairedDeviceIds = pairedDeviceIds,
                    )
                    devices[parsed.deviceId] = parsed
                } catch (_: SocketException) {
                    break
                } catch (_: IOException) {
                    continue
                } catch (_: IllegalArgumentException) {
                    continue
                }
            }

            return devices.values.sortedWith(
                compareByDescending<DiscoveredDesktop> { it.paired }
                    .thenBy { it.deviceName.lowercase() }
                    .thenBy { it.deviceId }
            )
        }
    }

    private fun parseDiscoveryPayload(
        payload: String,
        host: String,
        pairedDeviceIds: Set<String>,
    ): DiscoveredDesktop {
        val fields = splitFields(payload)
        require(fields.size == 5) { "invalid discovery payload field count" }
        require(fields[0] == DISCOVERY_PREFIX) { "invalid discovery payload prefix" }
        require(fields[1] == DISCOVERY_VERSION) { "unsupported discovery version" }

        val deviceId = unescapeField(fields[2])
        val deviceName = unescapeField(fields[3])
        val listenPort = fields[4].toInt().takeIf { it in 1..65535 }
            ?: throw IllegalArgumentException("invalid discovery port")

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
        require(!escaping) { "discovery payload ends with dangling escape" }
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
        require(!escaping) { "discovery field ends with dangling escape" }
        return result.toString()
    }

    companion object {
        private const val DISCOVERY_PORT = 38561
        private const val DISCOVERY_PREFIX = "LFT_DISCOVERY"
        private const val DISCOVERY_VERSION = "1"
        private const val MAX_PACKET_SIZE = 1024
        private const val RECEIVE_TIMEOUT_MS = 500
    }
}
