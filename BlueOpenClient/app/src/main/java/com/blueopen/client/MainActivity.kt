package com.blueopen.client

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.blueopen.client.databinding.ActivityMainBinding
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.security.MessageDigest
import java.util.UUID

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var bluetoothAdapter: BluetoothAdapter? = null
    private val matchedDevices = ArrayList<BluetoothDevice>()
    private var selectedDevice: BluetoothDevice? = null

    private val PREFS_NAME = "BlueOpenPrefs"
    private val KEY_LAST_DEVICE = "LastDeviceAddress"
    private val KEY_PASSWORD = "SavedPassword"

    // Custom Service UUID for RFCOMM (Must match the C# Server)
    private val SERVICE_UUID = UUID.fromString("4a982c5e-0c12-40f4-8a48-4a5f4a7c1b52")
    private val PERMISSION_REQUEST_CODE = 101
    private val REQUEST_ENABLE_BT = 102

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Initialize Bluetooth
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter

        if (bluetoothAdapter == null) {
            Toast.makeText(this, "Bluetooth is not supported on this device", Toast.LENGTH_LONG).show()
            finish()
            return
        }

        loadSettings()
        setupListeners()
        checkPermissionsAndLoadDevices()
    }

    private fun setupListeners() {
        binding.btnRefresh.setOnClickListener {
            checkPermissionsAndLoadDevices()
        }

        binding.btnLock.setOnClickListener {
            sendBluetoothCommand("LOCK")
        }

        binding.btnUnlock.setOnClickListener {
            sendBluetoothCommand("UNLOCK")
        }

        binding.spinnerDevices.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position in matchedDevices.indices) {
                    selectedDevice = matchedDevices[position]
                    saveDeviceAddress(selectedDevice!!.address)
                }
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
    }

    private fun checkPermissionsAndLoadDevices() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN
            )
        }

        val missingPermissions = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (missingPermissions.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missingPermissions.toTypedArray(), PERMISSION_REQUEST_CODE)
        } else {
            loadPairedDevices()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_CODE) {
            if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                loadPairedDevices()
            } else {
                Toast.makeText(this, "Bluetooth permissions are required for this app to work", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun loadPairedDevices() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_DENIED && 
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Toast.makeText(this, "No Bluetooth Connect permission", Toast.LENGTH_SHORT).show()
            return
        }

        if (bluetoothAdapter?.isEnabled == false) {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT)
            return
        }

        val pairedDevices: Set<BluetoothDevice>? = bluetoothAdapter?.bondedDevices
        matchedDevices.clear()
        val deviceNames = ArrayList<String>()

        val lastAddress = getLastSavedDeviceAddress()
        var selectedIndex = -1

        pairedDevices?.forEach { device ->
            matchedDevices.add(device)
            deviceNames.add("${device.name}\n[${device.address}]")
            if (device.address == lastAddress) {
                selectedIndex = matchedDevices.size - 1
            }
        }

        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, deviceNames)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.spinnerDevices.adapter = adapter

        if (selectedIndex != -1) {
            binding.spinnerDevices.setSelection(selectedIndex)
            selectedDevice = matchedDevices[selectedIndex]
        } else if (matchedDevices.isNotEmpty()) {
            selectedDevice = matchedDevices[0]
        }
    }

    private fun sendBluetoothCommand(command: String) {
        val device = selectedDevice
        val password = binding.etPassword.text.toString().trim()

        if (device == null) {
            Toast.makeText(this, "Please select a paired device", Toast.LENGTH_SHORT).show()
            return
        }

        if (password.isEmpty()) {
            Toast.makeText(this, "Please enter the security password", Toast.LENGTH_SHORT).show()
            return
        }

        // Save password for future convenience
        savePassword(password)

        updateUIStatus(getString(R.string.status_connecting), ContextCompat.getColor(this, R.color.text_secondary))

        // Bluetooth networking must be done on a background thread
        Thread {
            var socket: BluetoothSocket? = null
            try {
                if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_DENIED &&
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    runOnUiThread {
                        updateUIStatus("Permission Denied", ContextCompat.getColor(this, R.color.accent_red))
                        Toast.makeText(this@MainActivity, "Missing Bluetooth connect permission", Toast.LENGTH_LONG).show()
                    }
                    return@Thread
                }

                // Connect RFCOMM socket
                socket = device.createRfcommSocketToServiceRecord(SERVICE_UUID)
                socket.connect()

                runOnUiThread {
                    updateUIStatus(getString(R.string.status_authenticating), ContextCompat.getColor(this, R.color.primary_light))
                }

                val inputStream = socket.inputStream
                val outputStream = socket.outputStream
                val reader = BufferedReader(InputStreamReader(inputStream))

                // 1. Read challenge nonce from server
                val challengeNonce = reader.readLine()?.trim()
                if (challengeNonce.isNullOrEmpty()) {
                    throw Exception("No challenge received from server")
                }

                // 2. Compute SHA-256 hash
                val rawInput = challengeNonce + password
                val hashValue = computeSha256(rawInput)

                // 3. Send back "hash:COMMAND\n"
                val payload = "$hashValue:$command\n"
                outputStream.write(payload.toByteArray(Charsets.UTF_8))
                outputStream.flush()

                // 4. Read response
                val response = reader.readLine()?.trim()

                runOnUiThread {
                    if (response == "OK") {
                        if (command == "UNLOCK") {
                            updateUIStatus(getString(R.string.status_unlocked), ContextCompat.getColor(this@MainActivity, R.color.green_success))
                        } else {
                            updateUIStatus(getString(R.string.status_locked), ContextCompat.getColor(this@MainActivity, R.color.primary_indigo))
                        }
                    } else {
                        updateUIStatus(getString(R.string.status_failed), ContextCompat.getColor(this@MainActivity, R.color.accent_red))
                    }
                }

            } catch (e: Exception) {
                e.printStackTrace()
                runOnUiThread {
                    updateUIStatus("Connection Failed", ContextCompat.getColor(this@MainActivity, R.color.accent_red))
                    Toast.makeText(this@MainActivity, "Error: ${e.message}", Toast.LENGTH_LONG).show()
                }
            } finally {
                try {
                    socket?.close()
                } catch (e: Exception) {
                    // Ignore close exception
                }
            }
        }.start()
    }

    private fun updateUIStatus(text: String, color: Int) {
        binding.tvStatus.text = text
        binding.tvStatus.setTextColor(color)
        binding.statusIndicator.backgroundTintList = ColorStateList.valueOf(color)
    }

    private fun computeSha256(input: String): String {
        val md = MessageDigest.getInstance("SHA-256")
        val digest = md.digest(input.toByteArray(Charsets.UTF_8))
        return digest.fold("") { str, it -> str + "%02x".format(it) }
    }

    private fun saveDeviceAddress(address: String) {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_LAST_DEVICE, address).apply()
    }

    private fun getLastSavedDeviceAddress(): String? {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(KEY_LAST_DEVICE, null)
    }

    private fun savePassword(password: String) {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_PASSWORD, password).apply()
    }

    private fun loadSettings() {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedPassword = prefs.getString(KEY_PASSWORD, "")
        binding.etPassword.setText(savedPassword)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_ENABLE_BT) {
            if (resultCode == RESULT_OK) {
                loadPairedDevices()
            } else {
                Toast.makeText(this, "Bluetooth needs to be enabled to find devices", Toast.LENGTH_SHORT).show()
            }
        }
    }
}
