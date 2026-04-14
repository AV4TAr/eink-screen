# tools/

Helper scripts for the e-ink calendar display project.

---

## get_token.py

Generates a Google OAuth2 refresh token for the Calendar API.

```bash
pip install google-auth-oauthlib
python3 tools/get_token.py client_secret.json
```

---

## push.py — Send a push notification to the display

Publishes a JSON message to the `eink/push` MQTT topic. The display firmware
subscribes to this topic and renders the notification immediately.

**Install dependency (once):**

```bash
pip install paho-mqtt
```

**Usage:**

```bash
# Quick message (positional body)
python3 tools/push.py "Meeting room changed to 3B"

# Full options
python3 tools/push.py --title "Slack" --body "Diego: come to room 3" --source slack

# Remote broker
python3 tools/push.py --broker 192.168.1.5 "urgent message"
```

**Options:**

| Flag | Short | Default | Description |
|---|---|---|---|
| `--title` | `-t` | `Push` | Notification title shown on display |
| `--body` | `-m` | *(required)* | Message body |
| `--source` | `-s` | `cli` | Source label embedded in the payload |
| `--broker` | `-b` | `localhost` | MQTT broker hostname or IP |
| `--port` | `-p` | `1883` | MQTT broker port |

**Payload format** (`eink/push`):

```json
{"title": "Push", "body": "Meeting room changed to 3B", "source": "cli"}
```

---

## mosquitto/ — Local MQTT broker (Docker)

Runs a [Mosquitto](https://mosquitto.org/) MQTT broker on `localhost:1883`
using Docker Compose.

**Start the broker:**

```bash
cd tools/mosquitto
docker compose up -d
```

**Stop the broker:**

```bash
docker compose down
```

**Files:**

- `docker-compose.yml` — service definition
- `config/mosquitto.conf` — anonymous access, persistence enabled

---

## Configuring the display

Set `MQTT_BROKER` in `firmware/calendar_display/config.h` to the IP address of
the machine running the broker (use your LAN IP, not `localhost`):

```cpp
#define MQTT_BROKER "192.168.1.100"
#define MQTT_PORT   1883
```

The display will connect on boot and subscribe to `eink/push`.
