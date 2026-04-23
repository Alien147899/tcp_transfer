package com.example.lantransfer.model

data class LocalDevice(
    val deviceId: String,
    val deviceName: String,
)

data class PairedDevice(
    val deviceId: String,
    val host: String,
    val port: Int,
)

data class DiscoveredDesktop(
    val deviceId: String,
    val deviceName: String,
    val host: String,
    val port: Int,
    val paired: Boolean,
)

data class RecentTarget(
    val host: String,
    val port: Int,
    val targetDeviceId: String,
)

data class SelectedFile(
    val uriString: String,
    val displayName: String,
    val sizeBytes: Long,
)
