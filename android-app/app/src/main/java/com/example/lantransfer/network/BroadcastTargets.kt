package com.example.lantransfer.network

import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface

internal fun getBroadcastTargets(): List<InetAddress> {
    val targets = linkedSetOf<InetAddress>()
    targets += InetAddress.getByName("255.255.255.255")

    val interfaces = try {
        NetworkInterface.getNetworkInterfaces()?.toList().orEmpty()
    } catch (_: Exception) {
        emptyList()
    }

    for (networkInterface in interfaces) {
        if (!networkInterface.isUp || networkInterface.isLoopback) {
            continue
        }

        for (address in networkInterface.interfaceAddresses) {
            val broadcast = address.broadcast ?: continue
            if (address.address !is Inet4Address || broadcast !is Inet4Address) {
                continue
            }
            targets += broadcast
        }
    }

    return targets.toList()
}
