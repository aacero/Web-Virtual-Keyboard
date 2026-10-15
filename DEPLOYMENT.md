# Secure Deployment & Operation Guide

This guide details how to flash the firmware to ESP32 dongles and access them securely through isolated **Tailscale Serve** reverse proxies running on the host server **`deerpark`**.

---

## 1. Architecture

```text
[ Client Browser (Laptop / Phone / Tablet) ]
                  │
                  ▼  (HTTPS over Tailscale WireGuard Mesh)
[ Tailscale Nodes in Docker on deerpark ]
   ├── esp32-keyboard   ──▶ (LAN HTTP: 10.0.0.32:80)  ──▶ Dongle 1 ──▶ Target Machine 1
   └── esp32-keyboard-2 ──▶ (LAN HTTP: 10.0.0.223:80) ──▶ Dongle 2 ──▶ Target Machine 2
```

- **Encryption & Trust**: Tailscale automatically provisions valid Let's Encrypt TLS certificates for each dongle node.
- **Identity & Access Control**: Only devices authorized on your Tailscale tailnet can route to the keyboard web interfaces.
- **Portability**: Type into any target machine from any phone or computer on your tailnet, whether at home or remote.

---

## 2. Firmware Configuration & Flashing

The firmware is configured to obtain its network IP address automatically via **DHCP**.

### Step 1: Set Wi-Fi and Web Credentials
`Web-Virtual-Keyboard-platformio/include/config.h` is symlinked to `~/.config/esp32/config.h` (and ignored by git to prevent committing secrets). Template is provided at `include/config.example.h`.

Edit `~/.config/esp32/config.h` (or [`Web-Virtual-Keyboard-platformio/include/config.h`](file:///home/acero/src/Web-Virtual-Keyboard/Web-Virtual-Keyboard-platformio/include/config.h)):
```c
#define MASTER_USER     "your_admin_user"
#define MASTER_PASS     "your_strong_password"
#define WIFI_SSID       "YourWiFiNetwork"
#define WIFI_PASS       "YourWiFiPassword"
#define HOSTNAME        "web-virtual-keyboard-2" // Set a unique hostname per dongle
```

### Step 2: Build & Flash
Connect the ESP32 board to your computer via USB, then run:
```bash
pio run -d Web-Virtual-Keyboard-platformio -t upload
```

### Step 3: Note the Assigned DHCP IP
1. When the ESP32 finishes booting and connects to Wi-Fi, its DHCP IP (e.g. `10.0.0.32`, `10.0.0.223`) will be:
   - Displayed directly on the on-board color LCD screen (if using T-Display/T-Dongle).
   - Logged to the serial console (`pio device monitor -d Web-Virtual-Keyboard-platformio`).
2. **Recommended**: In your router's administration interface, create a **DHCP reservation (static lease)** for each ESP32's MAC address so the IPs never change.

---

## 3. Deerpark Tailscale Container Configuration

The reverse proxy containers are hosted on **`deerpark`** in `/srv/docker/esp32-keyboard-tailscale`.

### Directory Layout on `deerpark`:
```text
/srv/docker/esp32-keyboard-tailscale/
├── docker-compose.yml     # Multi-dongle container definitions
├── .env                   # Shared TS_AUTHKEY
├── serve.json             # Proxy config for dongle 1 (10.0.0.32:80)
├── serve-2.json           # Proxy config for dongle 2 (10.0.0.223:80)
├── serve-3.json           # Proxy config for dongle 3 (10.0.0.250:80)
└── update-ip.sh           # Helper script to update target IP
```

### Starting the Tailscale Nodes:
```bash
ssh -i ~/.ssh/hermes_automation acero@deerpark "cd /srv/docker/esp32-keyboard-tailscale && docker compose up -d"
```

### Checking Status & Logs:
```bash
ssh -i ~/.ssh/hermes_automation acero@deerpark "cd /srv/docker/esp32-keyboard-tailscale && docker compose logs -f"
```

---

## 4. Active Endpoints

| Dongle | Target LAN IP | Tailscale Node | URL |
| :--- | :--- | :--- | :--- |
| **Dongle 1** | `10.0.0.32:80` | `esp32-keyboard` (`100.69.69.58`) | `https://esp32-keyboard.tail39fd85.ts.net` |
| **Dongle 2** | `10.0.0.223:80` | `esp32-keyboard-2` (`100.69.69.70`) | `https://esp32-keyboard-2.tail39fd85.ts.net` |
| **Dongle 3** | `10.0.0.250:80` | `esp32-keyboard-3` (`100.69.69.14`) | `https://esp32-keyboard-3.tail39fd85.ts.net` |

Log in with the `MASTER_USER` and `MASTER_PASS` credentials set in `config.h`. Keystrokes and presets sent from the browser are encrypted over Tailscale and dispatched via the respective ESP32's native USB keyboard interface.

---

## 5. Tailscale Key Management & Expiration

### Auth Key Expiration
- **Current Key Expiration**: The registration Auth Key (`tskey-auth-kLy5ENe...`) was created on **October 14, 2026** with a 90-day validity and expires on **January 12, 2027**.
- **Important Distinction**:
  - The Auth Key expiration only restricts **registering new nodes** or spinning up fresh containers from scratch.
  - Existing registered nodes (`esp32-keyboard` and `esp32-keyboard-2`) persist their WireGuard credentials in `./tailscale-data/` and `./tailscale-data-2/` and will continue running uninterrupted even after the registration key expires.
  - **Machine Key Expiry**: To prevent Tailscale from logging out these long-lived server nodes after 180 days, go to the [Tailscale Admin Machines Console](https://login.tailscale.com/admin/machines), click the `...` menu next to `esp32-keyboard` and `esp32-keyboard-2`, and select **Disable key expiry**.

### How to Generate a New Auth Key
When flashing a third dongle or redeploying after January 12, 2027:
1. Open the [Tailscale Admin Console: Keys](https://login.tailscale.com/admin/settings/keys).
2. Click **Generate auth key...**.
3. Recommended settings:
   - **Reusable**: Enabled (allows multiple dongles to share one key).
   - **Expiration**: 90 days (or desired duration).
   - **Tags**: Apply tags if required by your ACLs (e.g. `tag:server`).
4. Copy the newly generated `tskey-auth-...` string.
5. Update the `.env` file on `deerpark`:
   ```bash
   ssh -i ~/.ssh/hermes_automation acero@deerpark "sed -i 's/TS_AUTHKEY=.*/TS_AUTHKEY=<NEW_AUTH_KEY>/' /srv/docker/esp32-keyboard-tailscale/.env"
   ```

