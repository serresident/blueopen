package com.blueopen.client

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.widget.Toast
import androidx.core.app.NotificationCompat
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL

class NetUnlockService : Service() {

    companion object {
        const val ACTION_START = "com.blueopen.client.ACTION_START"
        const val ACTION_STOP = "com.blueopen.client.ACTION_STOP"
        const val ACTION_CONFIRM_UNLOCK = "com.blueopen.client.ACTION_CONFIRM_UNLOCK"
        const val ACTION_REJECT_UNLOCK = "com.blueopen.client.ACTION_REJECT_UNLOCK"

        const val EXTRA_CHANNEL = "EXTRA_CHANNEL"
        const val EXTRA_REQUEST_ID = "EXTRA_REQUEST_ID"

        const val CHANNEL_ID_SERVICE = "blueopen_service_channel"
        const val CHANNEL_ID_UNLOCK = "blueopen_unlock_channel"

        const val NOTIFICATION_ID_SERVICE = 1001
        const val NOTIFICATION_ID_UNLOCK = 1002

        var isRunning = false
            private set
        var currentChannel: String? = null
            private set

        var onUnlockRequestReceived: ((requestId: String, machine: String, user: String) -> Unit)? = null
        var onStatusChanged: ((Boolean) -> Unit)? = null

        fun start(context: Context, channel: String) {
            val intent = Intent(context, NetUnlockService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_CHANNEL, channel)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            val intent = Intent(context, NetUnlockService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }

        fun confirmUnlock(context: Context, requestId: String, channel: String) {
            val intent = Intent(context, NetUnlockService::class.java).apply {
                action = ACTION_CONFIRM_UNLOCK
                putExtra(EXTRA_REQUEST_ID, requestId)
                putExtra(EXTRA_CHANNEL, channel)
            }
            context.startService(intent)
        }

        fun rejectUnlock(context: Context, requestId: String, channel: String) {
            val intent = Intent(context, NetUnlockService::class.java).apply {
                action = ACTION_REJECT_UNLOCK
                putExtra(EXTRA_REQUEST_ID, requestId)
                putExtra(EXTRA_CHANNEL, channel)
            }
            context.startService(intent)
        }
    }

    private val mainHandler = Handler(Looper.getMainLooper())
    private var streamThread: Thread? = null
    @Volatile private var shouldRun = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannels()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action ?: return START_NOT_STICKY

        when (action) {
            ACTION_START -> {
                val channel = intent.getStringExtra(EXTRA_CHANNEL)
                if (!channel.isNullOrBlank()) {
                    currentChannel = channel.trim()
                }
                if (currentChannel.isNullOrBlank()) {
                    val prefs = getSharedPreferences("BlueOpenPrefs", Context.MODE_PRIVATE)
                    currentChannel = prefs.getString("NetChannelId", null)
                }

                if (currentChannel.isNullOrBlank()) {
                    stopSelf()
                    return START_NOT_STICKY
                }

                startForegroundServiceInternal(currentChannel!!)
                startListeningLoop(currentChannel!!)
            }

            ACTION_STOP -> {
                stopListeningLoop()
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                } else {
                    @Suppress("DEPRECATION")
                    stopForeground(true)
                }
                stopSelf()
            }

            ACTION_CONFIRM_UNLOCK -> {
                val requestId = intent.getStringExtra(EXTRA_REQUEST_ID) ?: ""
                val channel = intent.getStringExtra(EXTRA_CHANNEL) ?: (currentChannel ?: "")
                dismissUnlockNotification()
                vibrateShort()
                sendAck(channel, requestId, "UNLOCK_CONFIRMED")
            }

            ACTION_REJECT_UNLOCK -> {
                val requestId = intent.getStringExtra(EXTRA_REQUEST_ID) ?: ""
                val channel = intent.getStringExtra(EXTRA_CHANNEL) ?: (currentChannel ?: "")
                dismissUnlockNotification()
                sendAck(channel, requestId, "UNLOCK_REJECTED")
            }
        }

        return START_STICKY
    }

    private fun createNotificationChannels() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

            val serviceChannel = NotificationChannel(
                CHANNEL_ID_SERVICE,
                "Служба BlueOpen Net",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Фоновая служба для получения запросов удаленной разблокировки"
                setShowBadge(false)
            }

            val unlockChannel = NotificationChannel(
                CHANNEL_ID_UNLOCK,
                "Запросы на вход в Windows",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Уведомления о запросах разблокировки компьютера"
                enableVibration(true)
                vibrationPattern = longArrayOf(0, 250, 150, 350)
                setShowBadge(true)
            }

            notificationManager.createNotificationChannel(serviceChannel)
            notificationManager.createNotificationChannel(unlockChannel)
        }
    }

    private fun startForegroundServiceInternal(channel: String) {
        val notification = buildForegroundNotification(channel)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID_SERVICE,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            )
        } else {
            startForeground(NOTIFICATION_ID_SERVICE, notification)
        }
        isRunning = true
        mainHandler.post { onStatusChanged?.invoke(true) }
    }

    private fun buildForegroundNotification(channel: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            openIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID_SERVICE)
            .setSmallIcon(R.drawable.app_logo)
            .setContentTitle("BlueOpen Net: в сети")
            .setContentText("Ожидание удаленной разблокировки ($channel)")
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .setContentIntent(pendingIntent)
            .build()
    }

    private fun startListeningLoop(channel: String) {
        stopListeningLoop()
        shouldRun = true

        streamThread = Thread {
            while (shouldRun) {
                var conn: HttpURLConnection? = null
                var reader: BufferedReader? = null
                try {
                    val url = URL("https://ntfy.sh/$channel/json?since=now")
                    conn = url.openConnection() as HttpURLConnection
                    conn.requestMethod = "GET"
                    conn.connectTimeout = 15000
                    conn.readTimeout = 65000 // ntfy sends ping every 45s
                    conn.doInput = true

                    val stream = conn.inputStream
                    reader = BufferedReader(InputStreamReader(stream, Charsets.UTF_8))

                    while (shouldRun) {
                        val line = reader.readLine() ?: break
                        val trimmed = line.trim()
                        if (trimmed.isEmpty() || trimmed.startsWith(":")) {
                            continue
                        }

                        try {
                            val eventDoc = JSONObject(trimmed)
                            val event = eventDoc.optString("event")
                            if (event == "message") {
                                val messageStr = eventDoc.optString("message", "")
                                handleReceivedMessage(messageStr)
                            }
                        } catch (pe: Exception) {
                            // Non-json or malformed line
                        }
                    }
                } catch (e: Exception) {
                    if (!shouldRun) break
                    try {
                        Thread.sleep(3000)
                    } catch (_: Exception) {}
                } finally {
                    try { reader?.close() } catch (_: Exception) {}
                    try { conn?.disconnect() } catch (_: Exception) {}
                }
            }
        }.apply {
            name = "BlueOpenNetStream"
            isDaemon = true
            start()
        }
    }

    private fun stopListeningLoop() {
        shouldRun = false
        isRunning = false
        mainHandler.post { onStatusChanged?.invoke(false) }
        try {
            streamThread?.interrupt()
            streamThread = null
        } catch (_: Exception) {}
    }

    private fun handleReceivedMessage(rawMsg: String) {
        var msgJson: JSONObject? = null
        if (rawMsg.startsWith("{") && rawMsg.endsWith("}")) {
            try {
                msgJson = JSONObject(rawMsg)
            } catch (_: Exception) {}
        }

        val type = msgJson?.optString("type") ?: ""

        if (type == "UNLOCK_REQUEST" || rawMsg.contains("UNLOCK_REQUEST")) {
            val reqId = msgJson?.optString("requestId") ?: ""
            val machine = msgJson?.optString("machine") ?: "Windows PC"
            val user = msgJson?.optString("user") ?: ""

            showUnlockNotification(reqId, machine, user)

            mainHandler.post {
                onUnlockRequestReceived?.invoke(reqId, machine, user)
            }
        } else if (type == "TEST" || rawMsg.contains("\"type\":\"TEST\"")) {
            val machine = msgJson?.optString("machine") ?: "Windows PC"
            mainHandler.post {
                Toast.makeText(applicationContext, "🔔 Тестовый сигнал от $machine получен!", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun showUnlockNotification(requestId: String, machine: String, user: String) {
        vibratePattern()

        val confirmIntent = Intent(this, NetUnlockService::class.java).apply {
            action = ACTION_CONFIRM_UNLOCK
            putExtra(EXTRA_REQUEST_ID, requestId)
            putExtra(EXTRA_CHANNEL, currentChannel)
        }
        val confirmPendingIntent = PendingIntent.getService(
            this,
            101,
            confirmIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val rejectIntent = Intent(this, NetUnlockService::class.java).apply {
            action = ACTION_REJECT_UNLOCK
            putExtra(EXTRA_REQUEST_ID, requestId)
            putExtra(EXTRA_CHANNEL, currentChannel)
        }
        val rejectPendingIntent = PendingIntent.getService(
            this,
            102,
            rejectIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val openAppIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
            putExtra("NET_REQUEST_ID", requestId)
            putExtra("NET_MACHINE", machine)
            putExtra("NET_USER", user)
            putExtra("NET_CHANNEL", currentChannel)
        }
        val openAppPendingIntent = PendingIntent.getActivity(
            this,
            103,
            openAppIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, CHANNEL_ID_UNLOCK)
            .setSmallIcon(R.drawable.app_logo)
            .setContentTitle("🌐 Запрос на вход: $machine")
            .setContentText("Пользователь: $user. Разблокировать ПК?")
            .setStyle(NotificationCompat.BigTextStyle().bigText("Компьютер $machine ожидает подтверждения входа через BlueOpen Net для пользователя $user."))
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setAutoCancel(true)
            .setContentIntent(openAppPendingIntent)
            .addAction(android.R.drawable.ic_media_play, "Разблокировать", confirmPendingIntent)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Отклонить", rejectPendingIntent)
            .build()

        val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.notify(NOTIFICATION_ID_UNLOCK, notification)
    }

    private fun dismissUnlockNotification() {
        val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.cancel(NOTIFICATION_ID_UNLOCK)
    }

    private fun sendAck(channel: String, requestId: String, action: String) {
        Thread {
            try {
                val ackUrl = URL("https://ntfy.sh/${channel}_ack")
                val conn = ackUrl.openConnection() as HttpURLConnection
                conn.requestMethod = "POST"
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", "application/json; charset=utf-8")

                val payload = JSONObject().apply {
                    put("action", action)
                    put("requestId", requestId)
                    put("time", System.currentTimeMillis())
                }

                conn.outputStream.use { os ->
                    os.write(payload.toString().toByteArray(Charsets.UTF_8))
                    os.flush()
                }

                val code = conn.responseCode
                conn.disconnect()

                mainHandler.post {
                    if (action == "UNLOCK_CONFIRMED") {
                        Toast.makeText(applicationContext, "✅ Вход на ПК разрешен!", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(applicationContext, "❌ Вход на ПК отклонен", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
                mainHandler.post {
                    Toast.makeText(applicationContext, "Ошибка отправки ответа: ${e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    private fun vibratePattern() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
                vibratorManager?.defaultVibrator?.vibrate(
                    VibrationEffect.createWaveform(longArrayOf(0, 250, 150, 350), -1)
                )
            } else {
                @Suppress("DEPRECATION")
                val vibrator = getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator?.vibrate(
                        VibrationEffect.createWaveform(longArrayOf(0, 250, 150, 350), -1)
                    )
                } else {
                    @Suppress("DEPRECATION")
                    vibrator?.vibrate(longArrayOf(0, 250, 150, 350), -1)
                }
            }
        } catch (_: Exception) {}
    }

    private fun vibrateShort() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
                vibratorManager?.defaultVibrator?.vibrate(
                    VibrationEffect.createOneShot(120, VibrationEffect.DEFAULT_AMPLITUDE)
                )
            } else {
                @Suppress("DEPRECATION")
                val vibrator = getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator?.vibrate(
                        VibrationEffect.createOneShot(120, VibrationEffect.DEFAULT_AMPLITUDE)
                    )
                } else {
                    @Suppress("DEPRECATION")
                    vibrator?.vibrate(120)
                }
            }
        } catch (_: Exception) {}
    }

    override fun onDestroy() {
        stopListeningLoop()
        super.onDestroy()
    }
}
