#!/usr/bin/env python3
"""Push a notification to the e-ink display via MQTT."""

import argparse
import json
import sys

try:
    import paho.mqtt.client as mqtt
    import paho.mqtt.publish as publish
except ImportError:
    print("Error: paho-mqtt is not installed. Run: pip install paho-mqtt", file=sys.stderr)
    sys.exit(1)

TOPIC = "eink/push"
DEFAULT_BROKER = "localhost"
DEFAULT_PORT = 1883
DEFAULT_TITLE = "Push"
DEFAULT_SOURCE = "cli"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Push a notification to the e-ink display via MQTT.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
examples:
  python3 tools/push.py "Meeting room changed to 3B"
  python3 tools/push.py --title "Slack" --body "Diego: come to room 3" --source slack
  python3 tools/push.py --broker 192.168.1.5 "urgent message"
        """,
    )
    parser.add_argument(
        "body_positional",
        nargs="?",
        metavar="BODY",
        help="Message body (shorthand for --body)",
    )
    parser.add_argument("--body", "-m", help="Message body text")
    parser.add_argument(
        "--title", "-t", default=DEFAULT_TITLE, help=f"Notification title (default: {DEFAULT_TITLE!r})"
    )
    parser.add_argument(
        "--source", "-s", default=DEFAULT_SOURCE, help=f"Source identifier (default: {DEFAULT_SOURCE!r})"
    )
    parser.add_argument(
        "--broker", "-b", default=DEFAULT_BROKER, help=f"MQTT broker host (default: {DEFAULT_BROKER!r})"
    )
    parser.add_argument(
        "--port", "-p", type=int, default=DEFAULT_PORT, help=f"MQTT broker port (default: {DEFAULT_PORT})"
    )
    return parser.parse_args()


def main():
    args = parse_args()

    # Resolve body: --body wins over positional; at least one is required
    body = args.body or args.body_positional
    if not body:
        print("Error: provide a message body as a positional argument or via --body", file=sys.stderr)
        sys.exit(1)

    payload = json.dumps({"title": args.title, "body": body, "source": args.source})

    try:
        publish.single(
            TOPIC,
            payload=payload,
            hostname=args.broker,
            port=args.port,
            client_id="eink-push-cli",
            keepalive=5,
        )
    except Exception as e:
        print(f"Error: could not connect to MQTT broker at {args.broker}:{args.port} — {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Sent to {TOPIC}: {args.title} — {body}")


if __name__ == "__main__":
    main()
