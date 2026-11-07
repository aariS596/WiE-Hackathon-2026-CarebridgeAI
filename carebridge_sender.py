"""Send parsed Arduino events to the CareBridge Live dashboard.


Your Arduino prints (9600 baud):
 PAIN:7              -> {"kind": "pain", "pain": 7}
 HELP_REQUEST        -> {"kind": "help", "help_kind": "Nurse assistance"}
 Temperature: 23.50 C  } buffered, then sent together as
 Humidity: 45.00%      } {"kind": "room", "temp_c": 23.5, "humidity": 45.0}


Env vars:
 CAREBRIDGE_URL     e.g. https://<your-app>/api/public/device
 DEVICE_INGEST_KEY  the device key stored in the project backend


Simulate without hardware:  python carebridge_sender.py --simulate
"""
import os, re, sys, time, random
import requests


URL = os.environ.get("CAREBRIDGE_URL", "")
KEY = os.environ.get("DEVICE_INGEST_KEY", "")


_pending_temp = None  # DHT11 temperature waiting for its humidity line




def send_event(body: dict) -> bool:
   """POST one event. Returns True when the backend stored it."""
   try:
       r = requests.post(URL, json=body, headers={"x-device-key": KEY}, timeout=5)
   except requests.RequestException as e:
       print("network error:", e)
       return False
   print(body, "->", r.status_code, r.text[:120])
   return r.status_code == 200




def handle_line(line: str):
   """Turn one serial line into a backend event (or None) and send it."""
   global _pending_temp
   line = line.strip()
   if m := re.fullmatch(r"PAIN:\s*(\d+(?:\.\d+)?)", line):
       return send_event({"kind": "pain", "pain": int(float(m.group(1)))})
   if line == "HELP_REQUEST":
       return send_event({"kind": "help", "help_kind": "Nurse assistance"})
   if m := re.fullmatch(
       r"(?:Temperature|TEMP):\s*(-?\d+(?:\.\d+)?)\s*C?",
       line,
       re.IGNORECASE
   ):
       _pending_temp = float(m.group(1))
       return None
   if m := re.fullmatch(
       r"HUMIDITY:\s*(\d+(?:\.\d+)?)\s*%?",
       line,
       re.IGNORECASE
   ):
       if _pending_temp is None:
           print("humidity without temperature, skipped")
           return None
       body = {"kind": "room", "temp_c": _pending_temp, "humidity": float(m.group(1))}
       _pending_temp = None
       return send_event(body)
   if line.startswith("LEVEL:") or line.startswith("STATUS |"):
       return None
  
   print("skip:", line)
   return None




def simulate(n: int = 10):
   """Feed fake serial lines through the same parser. Shows up in Live mode."""
   for _ in range(n):
       for line in random.choice([
           [f"PAIN:{random.randint(0, 10)}"],
           ["HELP_REQUEST"],
           [f"Temperature: {random.uniform(20, 26):.2f} C", f"Humidity: {random.uniform(35, 55):.2f}%"],
       ]):
           handle_line(line)
       time.sleep(1)




if __name__ == "__main__":
   if not URL or not KEY:
       sys.exit("Set CAREBRIDGE_URL and DEVICE_INGEST_KEY first.")
   if "--simulate" in sys.argv:
       simulate()
   else:
       import serial
       port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
       with serial.Serial(port, 9600, timeout=1) as ser:
           print("Listening on", port)
           while True:
               raw = ser.readline().decode(errors="ignore")
               if raw.strip():
                   handle_line(raw)
