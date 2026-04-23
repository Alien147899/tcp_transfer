package com.example.lantransfer.storage

import android.content.Context
import android.os.Build
import com.example.lantransfer.model.LocalDevice
import com.example.lantransfer.model.RecentTarget
import com.example.lantransfer.model.PairedDevice
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

class DeviceStore(context: Context) {
    private val prefs = context.getSharedPreferences("lan_transfer_store", Context.MODE_PRIVATE)

    fun getOrCreateLocalDevice(): LocalDevice {
        val existingId = prefs.getString(KEY_DEVICE_ID, null)
        val existingName = prefs.getString(KEY_DEVICE_NAME, null)
        if (existingId != null && existingName != null) {
            return LocalDevice(existingId, existingName)
        }

        val device = LocalDevice(
            deviceId = "android-" + UUID.randomUUID().toString().replace("-", ""),
            deviceName = defaultDeviceName(),
        )
        prefs.edit()
            .putString(KEY_DEVICE_ID, device.deviceId)
            .putString(KEY_DEVICE_NAME, device.deviceName)
            .apply()
        return device
    }

    fun loadPairedDevices(): List<PairedDevice> {
        val raw = prefs.getString(KEY_PAIRED_DEVICES_JSON, "[]").orEmpty()
        val jsonArray = JSONArray(raw)
        return buildList {
            for (index in 0 until jsonArray.length()) {
                val item = jsonArray.getJSONObject(index)
                add(
                    PairedDevice(
                        deviceId = item.getString("device_id"),
                        host = item.getString("host"),
                        port = item.getInt("port"),
                    )
                )
            }
        }
    }

    fun findPairedDevice(deviceId: String): PairedDevice? {
        return loadPairedDevices().firstOrNull { it.deviceId == deviceId }
    }

    fun upsertPairedDevice(device: PairedDevice) {
        val updated = loadPairedDevices().toMutableList()
        val index = updated.indexOfFirst { it.deviceId == device.deviceId }
        if (index >= 0) {
            val existing = updated[index]
            updated[index] = existing.copy(
                host = device.host.ifBlank { existing.host },
                port = device.port.takeIf { it != 0 } ?: existing.port,
            )
        } else {
            updated += device
        }

        val jsonArray = JSONArray()
        updated.forEach { entry ->
            jsonArray.put(
                JSONObject()
                    .put("device_id", entry.deviceId)
                    .put("host", entry.host)
                    .put("port", entry.port)
            )
        }
        prefs.edit().putString(KEY_PAIRED_DEVICES_JSON, jsonArray.toString()).apply()
    }

    fun removePairedDevice(deviceId: String) {
        val filtered = loadPairedDevices().filterNot { it.deviceId == deviceId }
        val jsonArray = JSONArray()
        filtered.forEach { entry ->
            jsonArray.put(
                JSONObject()
                    .put("device_id", entry.deviceId)
                    .put("host", entry.host)
                    .put("port", entry.port)
            )
        }
        prefs.edit().putString(KEY_PAIRED_DEVICES_JSON, jsonArray.toString()).apply()
    }

    fun loadRecentTarget(): RecentTarget {
        return RecentTarget(
            host = prefs.getString(KEY_LAST_HOST, "").orEmpty(),
            port = prefs.getInt(KEY_LAST_PORT, DEFAULT_PORT),
            targetDeviceId = prefs.getString(KEY_LAST_TARGET_DEVICE_ID, "").orEmpty(),
        )
    }

    fun saveRecentTarget(host: String, port: Int, targetDeviceId: String) {
        prefs.edit()
            .putString(KEY_LAST_HOST, host)
            .putInt(KEY_LAST_PORT, port)
            .putString(KEY_LAST_TARGET_DEVICE_ID, targetDeviceId)
            .apply()
    }

    fun loadReceiveFolderUri(): String {
        return prefs.getString(KEY_RECEIVE_FOLDER_URI, "").orEmpty()
    }

    fun loadReceiveFolderLabel(): String {
        return prefs.getString(KEY_RECEIVE_FOLDER_LABEL, "").orEmpty()
    }

    fun saveReceiveFolder(uriString: String, label: String) {
        prefs.edit()
            .putString(KEY_RECEIVE_FOLDER_URI, uriString)
            .putString(KEY_RECEIVE_FOLDER_LABEL, label)
            .apply()
    }

    private fun defaultDeviceName(): String {
        val manufacturer = Build.MANUFACTURER?.trim().orEmpty()
        val model = Build.MODEL?.trim().orEmpty()
        return listOf(manufacturer, model).filter { it.isNotBlank() }.joinToString(" ").ifBlank {
            "Android Phone"
        }
    }

    private companion object {
        const val DEFAULT_PORT = 9000
        const val KEY_DEVICE_ID = "device_id"
        const val KEY_DEVICE_NAME = "device_name"
        const val KEY_PAIRED_DEVICES_JSON = "paired_devices_json"
        const val KEY_LAST_HOST = "last_host"
        const val KEY_LAST_PORT = "last_port"
        const val KEY_LAST_TARGET_DEVICE_ID = "last_target_device_id"
        const val KEY_RECEIVE_FOLDER_URI = "receive_folder_uri"
        const val KEY_RECEIVE_FOLDER_LABEL = "receive_folder_label"
    }
}
