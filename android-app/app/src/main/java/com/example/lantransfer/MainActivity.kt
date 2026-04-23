package com.example.lantransfer

import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.provider.DocumentsContract
import android.text.format.Formatter
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.lantransfer.model.DiscoveredDesktop
import com.example.lantransfer.model.PairedDevice
import com.example.lantransfer.model.SelectedFile
import com.example.lantransfer.network.AndroidReceiverServer
import com.example.lantransfer.network.ConnectionCodeResolver
import com.example.lantransfer.network.DesktopDiscoveryScanner
import com.example.lantransfer.network.DesktopTransferClient
import com.example.lantransfer.storage.DeviceStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : AppCompatActivity() {
    private enum class BusyState {
        Idle,
        ResolvingCode,
        Scanning,
        Pairing,
        Sending,
    }

    private lateinit var deviceStore: DeviceStore
    private lateinit var client: DesktopTransferClient
    private val discoveryScanner = DesktopDiscoveryScanner()
    private val connectionCodeResolver = ConnectionCodeResolver()

    private lateinit var busyStatusText: TextView
    private lateinit var localDeviceText: TextView
    private lateinit var receiverStatusText: TextView
    private lateinit var receiverCodeText: TextView
    private lateinit var receiverFolderText: TextView
    private lateinit var discoveredDevicesContainer: LinearLayout
    private lateinit var discoveredDevicesEmptyText: TextView
    private lateinit var connectionCodeEditText: EditText
    private lateinit var receiverPortEditText: EditText
    private lateinit var hostEditText: EditText
    private lateinit var portEditText: EditText
    private lateinit var targetDeviceIdEditText: EditText
    private lateinit var selectedFileText: TextView
    private lateinit var sendProgressText: TextView
    private lateinit var pairedDevicesContainer: LinearLayout
    private lateinit var pairedDevicesText: TextView
    private lateinit var sendProgressBar: ProgressBar
    private lateinit var logText: TextView
    private lateinit var resolveCodeButton: Button
    private lateinit var startReceiverButton: Button
    private lateinit var stopReceiverButton: Button
    private lateinit var chooseReceiverFolderButton: Button
    private lateinit var openReceiverFolderButton: Button
    private lateinit var scanButton: Button
    private lateinit var pairButton: Button
    private lateinit var pickFileButton: Button
    private lateinit var sendButton: Button
    private lateinit var clearLogButton: Button

    private var selectedFile: SelectedFile? = null
    private var discoveredDevices: List<DiscoveredDesktop> = emptyList()
    private lateinit var receiverServer: AndroidReceiverServer

    private val filePicker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) {
                appendLog("File selection cancelled.")
                return@registerForActivityResult
            }
            contentResolver.takePersistableUriPermission(uri, IntentFlags.readOnlyFlags)
            selectedFile = resolveSelectedFile(uri)
            updateSelectedFileUi()
            appendLog("Selected file: ${selectedFile?.displayName}")
        }

    private val folderPicker =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri == null) {
                appendLog("Receive folder selection cancelled.")
                return@registerForActivityResult
            }
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            runCatching {
                contentResolver.takePersistableUriPermission(uri, flags)
            }.onFailure { ex ->
                appendLog("Failed to persist receive folder permission: ${ex.message}")
            }
            val label = describeTreeUri(uri)
            deviceStore.saveReceiveFolder(uri.toString(), label)
            refreshReceiveFolderUi()
            appendLog("Receive folder set to $label")
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        deviceStore = DeviceStore(this)
        client = DesktopTransferClient(contentResolver)
        receiverServer = AndroidReceiverServer(
            context = this,
            deviceStore = deviceStore,
            onLog = { message -> runOnUiThread { appendLog(message) } },
            onStatusChanged = { running, port, code ->
                runOnUiThread {
                    updateReceiverUi(running, port, code)
                }
            },
            onPairRequest = { requesterDeviceId ->
                confirmBlocking(
                    title = getString(R.string.incoming_pair_request_title),
                    message = "Allow pairing with device $requesterDeviceId?"
                )
            },
            onFileRequest = { senderDeviceId, fileName, fileSize ->
                confirmBlocking(
                    title = getString(R.string.incoming_file_request_title),
                    message = "Accept $fileName (${Formatter.formatFileSize(this, fileSize)}) from $senderDeviceId?"
                )
            },
        )

        bindViews()
        refreshLocalDevice()
        updateReceiverUi(false, receiverPortEditText.text.toString().trim().toIntOrNull() ?: 9000, receiverServer.currentCode())
        refreshReceiveFolderUi()
        restoreRecentTarget()
        refreshDiscoveredDevices()
        refreshPairedDevices()
        updateSelectedFileUi()
        handleIncomingIntent(intent)

        resolveCodeButton.setOnClickListener {
            lifecycleScope.launch {
                runResolveCode()
            }
        }

        startReceiverButton.setOnClickListener {
            startReceiver()
        }

        stopReceiverButton.setOnClickListener {
            stopReceiver()
        }

        chooseReceiverFolderButton.setOnClickListener {
            folderPicker.launch(null)
        }

        openReceiverFolderButton.setOnClickListener {
            openReceiveFolder()
        }

        scanButton.setOnClickListener {
            lifecycleScope.launch {
                runScan()
            }
        }

        pairButton.setOnClickListener {
            lifecycleScope.launch {
                runPairing()
            }
        }

        pickFileButton.setOnClickListener {
            filePicker.launch(arrayOf("*/*"))
        }

        sendButton.setOnClickListener {
            lifecycleScope.launch {
                runSend()
            }
        }

        clearLogButton.setOnClickListener {
            logText.text = ""
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIncomingIntent(intent)
    }

    override fun onDestroy() {
        receiverServer.stop()
        super.onDestroy()
    }

    private fun bindViews() {
        busyStatusText = findViewById(R.id.busyStatusText)
        localDeviceText = findViewById(R.id.localDeviceText)
        receiverStatusText = findViewById(R.id.receiverStatusText)
        receiverCodeText = findViewById(R.id.receiverCodeText)
        receiverFolderText = findViewById(R.id.receiverFolderText)
        discoveredDevicesContainer = findViewById(R.id.discoveredDevicesContainer)
        discoveredDevicesEmptyText = findViewById(R.id.discoveredDevicesEmptyText)
        connectionCodeEditText = findViewById(R.id.connectionCodeEditText)
        receiverPortEditText = findViewById(R.id.receiverPortEditText)
        hostEditText = findViewById(R.id.hostEditText)
        portEditText = findViewById(R.id.portEditText)
        targetDeviceIdEditText = findViewById(R.id.targetDeviceIdEditText)
        selectedFileText = findViewById(R.id.selectedFileText)
        sendProgressText = findViewById(R.id.sendProgressText)
        pairedDevicesContainer = findViewById(R.id.pairedDevicesContainer)
        pairedDevicesText = findViewById(R.id.pairedDevicesText)
        sendProgressBar = findViewById(R.id.sendProgressBar)
        logText = findViewById(R.id.logText)
        resolveCodeButton = findViewById(R.id.resolveCodeButton)
        startReceiverButton = findViewById(R.id.startReceiverButton)
        stopReceiverButton = findViewById(R.id.stopReceiverButton)
        chooseReceiverFolderButton = findViewById(R.id.chooseReceiverFolderButton)
        openReceiverFolderButton = findViewById(R.id.openReceiverFolderButton)
        scanButton = findViewById(R.id.scanButton)
        pairButton = findViewById(R.id.pairButton)
        pickFileButton = findViewById(R.id.pickFileButton)
        sendButton = findViewById(R.id.sendButton)
        clearLogButton = findViewById(R.id.clearLogButton)
    }

    private suspend fun runResolveCode() {
        val code = connectionCodeEditText.text.toString().trim()
        if (code.length != 6 || !code.all { it.isDigit() }) {
            appendLog("Connection code must be exactly 6 digits.")
            return
        }

        setBusy(BusyState.ResolvingCode)
        try {
            appendLog("Resolving connection code $code ...")
            val resolved = withContext(Dispatchers.IO) {
                connectionCodeResolver.resolve(
                    code = code,
                    pairedDeviceIds = deviceStore.loadPairedDevices().map { it.deviceId }.toSet(),
                )
            }
            if (resolved == null) {
                appendLog("No receiver responded to connection code $code.")
                return
            }

            hostEditText.setText(resolved.host)
            portEditText.setText(resolved.port.toString())
            discoveredDevices = mergeResolvedDevice(discoveredDevices, resolved)
            refreshDiscoveredDevices()
            val localDevice = deviceStore.getOrCreateLocalDevice()
            val targetDeviceId = if (resolved.paired) {
                resolved.deviceId
            } else {
                appendLog("Resolved ${resolved.deviceName}. Pairing immediately ...")
                withContext(Dispatchers.IO) {
                    client.pair(resolved.host, resolved.port, localDevice.deviceId)
                }
            }

            deviceStore.upsertPairedDevice(PairedDevice(targetDeviceId, resolved.host, resolved.port))
            deviceStore.saveRecentTarget(resolved.host, resolved.port, targetDeviceId)
            targetDeviceIdEditText.setText(targetDeviceId)
            discoveredDevices = discoveredDevices.map { discovered ->
                if (discovered.host == resolved.host && discovered.port == resolved.port) {
                    discovered.copy(deviceId = targetDeviceId, paired = true)
                } else {
                    discovered
                }
            }
            refreshDiscoveredDevices()
            refreshPairedDevices()
            appendLog("Code paired successfully with ${resolved.deviceName}")
        } catch (ex: Exception) {
            appendLog("Resolve code failed: ${ex.message}")
        } finally {
            setBusy(BusyState.Idle)
        }
    }

    private suspend fun runScan() {
        setBusy(BusyState.Scanning)
        try {
            appendLog("Scanning nearby receivers ...")
            discoveredDevices = withContext(Dispatchers.IO) {
                discoveryScanner.scan(
                    durationMs = 4500,
                    pairedDeviceIds = deviceStore.loadPairedDevices().map { it.deviceId }.toSet(),
                )
            }
            refreshDiscoveredDevices()
            appendLog(
                if (discoveredDevices.isEmpty()) {
                    "No online receivers found."
                } else {
                    "Scan complete. Found ${discoveredDevices.size} receiver device(s)."
                }
            )
        } catch (ex: Exception) {
            appendLog("Scan failed: ${ex.message}")
        } finally {
            setBusy(BusyState.Idle)
        }
    }

    private suspend fun runPairing() {
        val host = hostEditText.text.toString().trim()
        val port = parsePort()
        if (host.isEmpty() || port == null) {
            appendLog("Pairing requires a valid host and port.")
            return
        }

        val localDevice = deviceStore.getOrCreateLocalDevice()
        setBusy(BusyState.Pairing)
        try {
            appendLog("Pairing with $host:$port ...")
            val remoteDeviceId = withContext(Dispatchers.IO) {
                client.pair(host, port, localDevice.deviceId)
            }
            deviceStore.upsertPairedDevice(PairedDevice(remoteDeviceId, host, port))
            deviceStore.saveRecentTarget(host, port, remoteDeviceId)
            targetDeviceIdEditText.setText(remoteDeviceId)
            discoveredDevices = discoveredDevices.map { discovered ->
                if (discovered.host == host && discovered.port == port) {
                    discovered.copy(deviceId = remoteDeviceId, paired = true)
                } else {
                    discovered
                }
            }
            refreshDiscoveredDevices()
            refreshPairedDevices()
            appendLog("Pairing completed. Remote device: $remoteDeviceId")
        } catch (ex: Exception) {
            appendLog("Pairing failed: ${ex.message}")
        } finally {
            setBusy(BusyState.Idle)
        }
    }

    private suspend fun runSend() {
        val host = hostEditText.text.toString().trim()
        val port = parsePort()
        val targetDeviceId = targetDeviceIdEditText.text.toString().trim()
        val file = selectedFile

        if (host.isEmpty() || port == null || targetDeviceId.isEmpty() || file == null) {
            appendLog("Send requires host, port, target device ID, and a selected file.")
            return
        }
        if (file.sizeBytes <= 0L) {
            appendLog("Selected file size is invalid.")
            return
        }
        if (file.sizeBytes > 8L * 1024L * 1024L * 1024L) {
            appendLog("Selected file exceeds the current receiver limit of 8 GiB.")
            return
        }

        val pairedDevice = deviceStore.findPairedDevice(targetDeviceId)
        if (pairedDevice == null) {
            appendLog("Target device is not paired: $targetDeviceId")
            return
        }
        if (pairedDevice.host.isNotEmpty() && pairedDevice.host != host) {
            appendLog("Target host does not match paired device record.")
            return
        }
        if (pairedDevice.port != 0 && pairedDevice.port != port) {
            appendLog("Target port does not match paired device record.")
            return
        }

        setBusy(BusyState.Sending)
        try {
            deviceStore.saveRecentTarget(host, port, targetDeviceId)
            updateProgress(0L, file.sizeBytes)
            appendLog("Sending ${file.displayName} to $host:$port ...")
            withContext(Dispatchers.IO) {
                client.sendFile(
                    host = host,
                    port = port,
                    localDeviceId = deviceStore.getOrCreateLocalDevice().deviceId,
                    pairedDevice = pairedDevice,
                    uri = Uri.parse(file.uriString),
                    displayName = file.displayName,
                    sizeBytes = file.sizeBytes,
                    onProgress = { sentBytes, totalBytes ->
                        runOnUiThread {
                            updateProgress(sentBytes, totalBytes)
                        }
                    },
                )
            }
            sendProgressText.text = getString(R.string.transfer_progress_done)
            sendProgressBar.progress = 100
            appendLog("File sent successfully.")
        } catch (ex: Exception) {
            appendLog("Send failed: ${ex.message}")
        } finally {
            setBusy(BusyState.Idle)
        }
    }

    private fun refreshLocalDevice() {
        val localDevice = deviceStore.getOrCreateLocalDevice()
        localDeviceText.text = "Local device: ${localDevice.deviceName} [${localDevice.deviceId}]"
    }

    private fun startReceiver() {
        val port = receiverPortEditText.text.toString().trim().toIntOrNull()?.takeIf { it in 1..65535 }
        if (port == null) {
            appendLog("Receiver requires a valid listen port.")
            return
        }
        receiverServer.start(port)
        appendLog("Receiver started on phone port $port")
    }

    private fun stopReceiver() {
        receiverServer.stop()
        appendLog("Receiver stopped on phone.")
    }

    private fun updateReceiverUi(running: Boolean, port: Int, code: String) {
        receiverStatusText.text = getString(
            if (running) R.string.receiver_status_running else R.string.receiver_status_stopped
        )
        receiverCodeText.text = code
        receiverPortEditText.setText(port.toString())
        receiverPortEditText.isEnabled = !running
        startReceiverButton.isEnabled = !running
        stopReceiverButton.isEnabled = running
    }

    private fun refreshReceiveFolderUi() {
        receiverFolderText.text = deviceStore.loadReceiveFolderLabel().ifBlank {
            getString(R.string.receiver_save_path_default)
        }
    }

    private fun openReceiveFolder() {
        val uriString = deviceStore.loadReceiveFolderUri()
        if (uriString.isBlank()) {
            appendLog(getString(R.string.receive_folder_not_selected))
            return
        }

        try {
            val treeUri = Uri.parse(uriString)
            val documentUri = DocumentsContract.buildDocumentUriUsingTree(
                treeUri,
                DocumentsContract.getTreeDocumentId(treeUri)
            )

            val directOpenIntent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(documentUri, DocumentsContract.Document.MIME_TYPE_DIR)
                addFlags(
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                        Intent.FLAG_ACTIVITY_NEW_TASK
                )
            }
            startActivity(directOpenIntent)
        } catch (_: Exception) {
            try {
                val fallbackIntent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
                    putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(uriString))
                    addFlags(
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                            Intent.FLAG_ACTIVITY_NEW_TASK
                    )
                }
                startActivity(fallbackIntent)
            } catch (_: Exception) {
                appendLog(getString(R.string.receive_folder_open_failed))
            }
        }
    }

    private fun refreshDiscoveredDevices() {
        discoveredDevicesContainer.removeAllViews()
        discoveredDevicesEmptyText.visibility = if (discoveredDevices.isEmpty()) View.VISIBLE else View.GONE
        if (discoveredDevices.isEmpty()) {
            discoveredDevicesEmptyText.text = getString(R.string.scan_empty)
            return
        }
        discoveredDevices.forEach { device ->
            discoveredDevicesContainer.addView(buildDiscoveredDeviceRow(device))
        }
    }

    private fun mergeResolvedDevice(
        existing: List<DiscoveredDesktop>,
        resolved: DiscoveredDesktop,
    ): List<DiscoveredDesktop> {
        val mutable = existing.toMutableList()
        val index = mutable.indexOfFirst {
            it.deviceId == resolved.deviceId || (it.host == resolved.host && it.port == resolved.port)
        }
        if (index >= 0) {
            mutable[index] = resolved
        } else {
            mutable += resolved
        }
        return mutable.sortedWith(
            compareByDescending<DiscoveredDesktop> { it.paired }
                .thenBy { it.deviceName.lowercase() }
                .thenBy { it.deviceId }
        )
    }

    private fun restoreRecentTarget() {
        val recentTarget = deviceStore.loadRecentTarget()
        if (recentTarget.host.isNotBlank()) {
            hostEditText.setText(recentTarget.host)
        }
        if (recentTarget.port in 1..65535) {
            portEditText.setText(recentTarget.port.toString())
        }
        if (recentTarget.targetDeviceId.isNotBlank()) {
            targetDeviceIdEditText.setText(recentTarget.targetDeviceId)
        }
    }

    private fun refreshPairedDevices() {
        val pairedDevices = deviceStore.loadPairedDevices()
        pairedDevicesContainer.removeAllViews()
        pairedDevicesText.visibility = if (pairedDevices.isEmpty()) View.VISIBLE else View.GONE
        if (pairedDevices.isEmpty()) {
            pairedDevicesText.text = getString(R.string.paired_devices_empty)
            return
        }

        pairedDevices.forEach { device ->
            pairedDevicesContainer.addView(buildPairedDeviceRow(device))
        }
    }

    private fun resolveSelectedFile(uri: Uri): SelectedFile? {
        val cursor = contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE),
            null,
            null,
            null
        )
        cursor.use { c: Cursor? ->
            if (c != null && c.moveToFirst()) {
                val name = c.getString(c.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
                val size = c.getLong(c.getColumnIndexOrThrow(OpenableColumns.SIZE))
                return SelectedFile(uri.toString(), name, size)
            }
        }
        return null
    }

    private fun parsePort(): Int? {
        return portEditText.text.toString().trim().toIntOrNull()?.takeIf { it in 1..65535 }
    }

    private fun buildPairedDeviceRow(device: PairedDevice): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 0, 0, dp(10))
        }
        val deviceButton = Button(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            text = "${device.deviceId}\n${device.host}:${device.port}"
            isAllCaps = false
            setOnClickListener {
                hostEditText.setText(device.host)
                portEditText.setText(device.port.toString())
                targetDeviceIdEditText.setText(device.deviceId)
                deviceStore.saveRecentTarget(device.host, device.port, device.deviceId)
                appendLog("Loaded paired device: ${device.deviceId}")
            }
        }
        val removeButton = Button(this).apply {
            text = getString(R.string.action_remove)
            setOnClickListener {
                deviceStore.removePairedDevice(device.deviceId)
                refreshPairedDevices()
                discoveredDevices = discoveredDevices.map { discovered ->
                    if (discovered.deviceId == device.deviceId) {
                        discovered.copy(paired = false)
                    } else {
                        discovered
                    }
                }
                refreshDiscoveredDevices()
                appendLog("Removed paired device: ${device.deviceId}")
            }
        }
        row.addView(deviceButton)
        row.addView(removeButton)
        return row
    }

    private fun buildDiscoveredDeviceRow(device: DiscoveredDesktop): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 0, 0, dp(10))
        }
        val detailsButton = Button(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            val status = if (device.paired) "paired" else "new"
            text = "${device.deviceName}\n${device.host}:${device.port}  [$status]"
            isAllCaps = false
            setOnClickListener {
                hostEditText.setText(device.host)
                portEditText.setText(device.port.toString())
                if (device.paired) {
                    targetDeviceIdEditText.setText(device.deviceId)
                    deviceStore.saveRecentTarget(device.host, device.port, device.deviceId)
                    appendLog("Loaded online receiver: ${device.deviceName}")
                }
            }
        }
        val actionButton = Button(this).apply {
            text = getString(if (device.paired) R.string.action_use else R.string.action_pair)
            setOnClickListener {
                lifecycleScope.launch {
                    if (device.paired) {
                        hostEditText.setText(device.host)
                        portEditText.setText(device.port.toString())
                        targetDeviceIdEditText.setText(device.deviceId)
                        deviceStore.saveRecentTarget(device.host, device.port, device.deviceId)
                        appendLog("Using paired receiver: ${device.deviceName}")
                    } else {
                        pairDiscoveredDevice(device)
                    }
                }
            }
        }
        row.addView(detailsButton)
        row.addView(actionButton)
        return row
    }

    private suspend fun pairDiscoveredDevice(device: DiscoveredDesktop) {
        hostEditText.setText(device.host)
        portEditText.setText(device.port.toString())
        setBusy(BusyState.Pairing)
        try {
            val localDevice = deviceStore.getOrCreateLocalDevice()
            appendLog("Pairing with ${device.deviceName} at ${device.host}:${device.port} ...")
            val remoteDeviceId = withContext(Dispatchers.IO) {
                client.pair(device.host, device.port, localDevice.deviceId)
            }
            deviceStore.upsertPairedDevice(PairedDevice(remoteDeviceId, device.host, device.port))
            deviceStore.saveRecentTarget(device.host, device.port, remoteDeviceId)
            targetDeviceIdEditText.setText(remoteDeviceId)
            discoveredDevices = discoveredDevices.map { discovered ->
                if (discovered.host == device.host && discovered.port == device.port) {
                    discovered.copy(deviceId = remoteDeviceId, paired = true)
                } else {
                    discovered
                }
            }
            refreshDiscoveredDevices()
            refreshPairedDevices()
            appendLog("Pairing completed. Remote device: $remoteDeviceId")
        } catch (ex: Exception) {
            appendLog("Pairing failed: ${ex.message}")
        } finally {
            setBusy(BusyState.Idle)
        }
    }

    private fun updateSelectedFileUi() {
        selectedFileText.text = selectedFile?.let {
            "${it.displayName} (${Formatter.formatFileSize(this, it.sizeBytes)})"
        } ?: getString(R.string.no_file_selected)
        if (selectedFile == null) {
            sendProgressBar.progress = 0
            sendProgressText.text = getString(R.string.transfer_progress_idle)
        }
    }

    private fun updateProgress(sentBytes: Long, totalBytes: Long) {
        val safeTotal = totalBytes.coerceAtLeast(1L)
        val progress = ((sentBytes * 100L) / safeTotal).toInt().coerceIn(0, 100)
        sendProgressBar.progress = progress
        sendProgressText.text =
            "${Formatter.formatFileSize(this, sentBytes)} / ${Formatter.formatFileSize(this, totalBytes)} ($progress%)"
    }

    private fun handleIncomingIntent(intent: Intent?) {
        if (intent?.action != Intent.ACTION_SEND) {
            return
        }
        val uri = extractStreamUri(intent) ?: return
        if ((intent.flags and IntentFlags.readOnlyFlags) != 0) {
            runCatching {
                contentResolver.takePersistableUriPermission(uri, IntentFlags.readOnlyFlags)
            }
        }
        selectedFile = resolveSelectedFile(uri)
        updateSelectedFileUi()
        appendLog("Received shared file: ${selectedFile?.displayName}")
    }

    private fun confirmBlocking(title: String, message: String): Boolean {
        val result = java.util.concurrent.CountDownLatch(1)
        var accepted = false
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    accepted = true
                    result.countDown()
                }
                .setNegativeButton(android.R.string.cancel) { _, _ ->
                    accepted = false
                    result.countDown()
                }
                .show()
        }
        result.await()
        return accepted
    }

    private fun appendLog(message: String) {
        val current = logText.text?.toString().orEmpty()
        logText.text = if (current.isBlank()) message else "$message\n$current"
    }

    private fun describeTreeUri(uri: Uri): String {
        val treeId = DocumentsContract.getTreeDocumentId(uri)
        return treeId.substringAfter(':', treeId)
            .replace('/', ' ')
            .ifBlank { uri.toString() }
    }

    private fun extractStreamUri(intent: Intent): Uri? {
        @Suppress("DEPRECATION")
        return intent.getParcelableExtra(Intent.EXTRA_STREAM)
    }

    private fun setBusy(state: BusyState) {
        val busy = state != BusyState.Idle
        resolveCodeButton.isEnabled = !busy
        scanButton.isEnabled = !busy
        pairButton.isEnabled = !busy
        pickFileButton.isEnabled = !busy
        sendButton.isEnabled = !busy
        clearLogButton.isEnabled = !busy
        busyStatusText.visibility = if (busy) View.VISIBLE else View.GONE
        busyStatusText.text = when (state) {
            BusyState.Idle -> ""
            BusyState.ResolvingCode -> getString(R.string.busy_resolving_code)
            BusyState.Scanning -> getString(R.string.busy_scanning)
            BusyState.Pairing -> getString(R.string.busy_pairing)
            BusyState.Sending -> getString(R.string.busy_sending)
        }
    }

    private fun dp(value: Int): Int {
        return (value * resources.displayMetrics.density).toInt()
    }
}

private object IntentFlags {
    const val readOnlyFlags = android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
}
