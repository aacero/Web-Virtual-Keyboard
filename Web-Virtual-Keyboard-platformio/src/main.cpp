#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "storage.h"
#include "globals.h"
#include "http_handlers.h"
#include "display.h"
#include "sd_card.h"
#include "pinout.h"


bool connectWiFi()
{
	LogSerial.printf("[WiFi] Connecting to \"%s\"...", WIFI_SSID);

	WiFi.setHostname(HOSTNAME);
	WiFi.mode(WIFI_MODE_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASS);

	const uint32_t start = millis();

	while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000U)
	{
		delay(250);
		LogSerial.print('.');
	}

	LogSerial.print("\r\n");

	if (WiFi.status() == WL_CONNECTED)
	{
		LogSerial.printf("[WiFi] OK: %s\r\n", WiFi.localIP().toString().c_str());
		return true;
	}

	LogSerial.print("[WiFi] FAILED (timeout).\r\n");
	return false;
}

// ---------- Setup / Loop ----------
void setup()
{
	// UART for logging over an external USB-UART adapter (not CDC)
	LogSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
	delay(100);
	LogSerial.print("\r\n=== Web Keyboard Server ===\r\n");

	USB.begin();
	Serial.begin(115200);
	delay(200);
	Keyboard.begin();
	LogSerial.print("[USB] HID Keyboard ready\r\n");
	delay(200);

	// Check storage version and wipe if incompatible; ensures presets key exists
	storageInit();

	// Initialize MicroSD card
	sdInit();

#if ENABLE_DISPLAY
	display_init();

	if(connectWiFi())
	{
		display_write_word(COLOR_OK, Align::RIGHT, 5, WiFi.localIP().toString().c_str());
	}
	else
	{
		display_write_word(COLOR_ERROR, Align::RIGHT, 5, "Disconnected");
	}
#else
	(void)connectWiFi();
#endif

	// Onboard button configuration (GPIO 0)
	pinMode(PIN_BUTTON, INPUT_PULLUP);

	// HTTP routes
	server.on("/", HTTP_GET, handleRoot);
	server.on("/type", HTTP_POST, handleType);
	server.on("/info", HTTP_GET, handleGetInfo);
	server.on("/reboot", HTTP_POST, handleReboot);
	server.on("/update", HTTP_POST, handleUpdate, handleUpdateChunk);
	server.on("/presets", HTTP_GET, handleGetPresets);
	server.on("/presets", HTTP_POST, handlePostPreset);
	server.on("/presets", HTTP_DELETE, handleDeletePreset);
	server.on("/send", HTTP_POST, handleSendPreset);

	// MicroSD & Payload routes
	server.on("/sd/status", HTTP_GET, handleSdStatus);
	server.on("/sd/list", HTTP_GET, handleSdList);
	server.on("/sd/file", HTTP_GET, handleSdFile);
	server.on("/sd/upload", HTTP_POST, handleSdUpload, handleSdUploadChunk);
	server.on("/sd/save", HTTP_POST, handleSdSave);
	server.on("/sd/delete", HTTP_POST, handleSdDelete);
	server.on("/sd/run", HTTP_POST, handleSdRun);

	server.on("/favicon.ico", HTTP_GET, []()
	{
		server.send(204);
	});

	server.on("/manifest.json", HTTP_GET, []()
	{
		server.send(200, "application/manifest+json",
			"{\"name\":\"Web Virtual Keyboard\",\"short_name\":\"Keyboard\",\"start_url\":\"/\","
			"\"display\":\"standalone\",\"background_color\":\"#121315\",\"theme_color\":\"#ff7a18\"}");
	});

	server.begin();
	LogSerial.print("[HTTP] Server started on port ");
	LogSerial.println(SERVER_PORT);
}

void loop()
{
	server.handleClient();

	const uint32_t now = millis();

	// ---------- 1. Button Handling (GPIO 0) ----------
	static uint32_t btnPressStart = 0;
	static bool btnHeldHandled = false;

	if (digitalRead(PIN_BUTTON) == LOW)
	{
		if (btnPressStart == 0)
		{
			btnPressStart = now;
			btnHeldHandled = false;
		}
		else if (!btnHeldHandled && (now - btnPressStart >= 1500))
		{
			btnHeldHandled = true;
			LogSerial.println("[Button] Long press (>=1.5s) -> Rebooting ESP32...");
#if ENABLE_DISPLAY
			display_write_word(COLOR_WARN, Align::RIGHT, 5, "Rebooting...");
#endif
			delay(500);
			ESP.restart();
		}
	}
	else
	{
		if (btnPressStart != 0 && !btnHeldHandled)
		{
			// Short press (<1.5s): force immediate Wi-Fi reconnect or display update
			const uint32_t duration = now - btnPressStart;
			if (duration >= 50)
			{
				LogSerial.println("[Button] Short press -> checking WiFi / updating display...");
				if (WiFi.status() != WL_CONNECTED)
				{
					WiFi.reconnect();
				}
#if ENABLE_DISPLAY
				if (WiFi.status() == WL_CONNECTED)
				{
					display_write_word(COLOR_OK, Align::RIGHT, 5, WiFi.localIP().toString().c_str());
				}
				else
				{
					display_write_word(COLOR_ERROR, Align::RIGHT, 5, "Disconnected");
				}
#endif
			}
		}
		btnPressStart = 0;
		btnHeldHandled = false;
	}

	// ---------- 2. Wi-Fi Auto-Reconnect Watchdog ----------
	static uint32_t lastWifiCheck = 0;
	static uint32_t wifiDisconnectStart = 0;

	if (now - lastWifiCheck >= 10000)
	{
		lastWifiCheck = now;
		if (WiFi.status() != WL_CONNECTED)
		{
			if (wifiDisconnectStart == 0)
			{
				wifiDisconnectStart = now;
				LogSerial.println("[WiFi] Lost connection!");
#if ENABLE_DISPLAY
				display_write_word(COLOR_ERROR, Align::RIGHT, 5, "Reconnecting...");
#endif
			}
			LogSerial.println("[WiFi] Attempting reconnect...");
			WiFi.reconnect();

			// If disconnected for > 3 minutes (180,000 ms), reboot to self-heal
			if (now - wifiDisconnectStart >= 180000)
			{
				LogSerial.println("[WiFi] Disconnected >3m -> Self-heal rebooting...");
#if ENABLE_DISPLAY
				display_write_word(COLOR_ERROR, Align::RIGHT, 5, "Rebooting...");
#endif
				delay(500);
				ESP.restart();
			}
		}
		else
		{
			if (wifiDisconnectStart != 0)
			{
				LogSerial.printf("[WiFi] Reconnected: %s\r\n", WiFi.localIP().toString().c_str());
#if ENABLE_DISPLAY
				display_write_word(COLOR_OK, Align::RIGHT, 5, WiFi.localIP().toString().c_str());
#endif
				wifiDisconnectStart = 0;
			}
		}
	}
}