# CarebridgeAI

**Best Healthcare Hack - Women in Engineering Hackathon 2026, University of Waterloo**
Healthcare category sponsored by PointClickCare.

An Arduino-based patient-reporting prototype with an LCD interface and a web dashboard for healthcare workers.

## Features

- Joystick pain reporting and a help button with buzzer and light feedback
- Temperature readings from a DHT11 sensor
- Timestamped pain charts and acknowledgment of assistance requests
- AI-generated summaries and a priority view based on reported pain and unacknowledged calls

## How it works

The Arduino sends pain reports, help requests and sensor readings over serial. The Python bridge reads these events and posts them to the web dashboard's backend.

## Code and demo

- `Carebridge_AI.ino` - Arduino code
- `carebridge_sender.py` - Python serial-to-dashboard bridge
- `CareBridge-intro.mp4` - demo video
- Live dashboard: https://prompt-spark-webworks.lovable.app/

Built as a hackathon prototype, not validated for clinical use.
