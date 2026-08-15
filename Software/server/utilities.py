import os
import csv
import json
import random
import threading
import time
from datetime import datetime

# Dedicated CSV Data Storage Configuration
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
CSV_FILE_PATH = os.path.join(DATA_DIR, "targets.csv")

# Global State Variables
serial_conn = None
serial_thread = None
db_lock = threading.Lock()

# Shared HMI state
hmi_state = {
    "connected": False,
    "port": None,
    "rover_lat": 23.8,
    "rover_lon": 90.35,
    "heading": 45,
    "battery": 82,
    "tilt_x": 0,
    "tilt_y": 0,
    "speed": 0,
    "sensitivity": 50,
    "autonomous": False,
    "logs": [],
    "last_update": 0
}

def initialize_database():
    """Initializes the dedicated CSV database file inside the data/ folder."""
    global DATA_DIR, CSV_FILE_PATH
    os.makedirs(DATA_DIR, exist_ok=True)
    if not os.path.exists(CSV_FILE_PATH):
        with open(CSV_FILE_PATH, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(["Timestamp", "Latitude", "Longitude", "Signal"])
        print(f"Created dedicated CSV database at: {CSV_FILE_PATH}")
    else:
        print(f"Using dedicated CSV database at: {CSV_FILE_PATH}")

def add_database_row(lat, lon, signal, timestamp=None):
    """Adds a detected target row to the dedicated CSV database in a thread-safe manner."""
    global CSV_FILE_PATH
    with db_lock:
        if not timestamp:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        # Ensure directory and file exist
        if not os.path.exists(CSV_FILE_PATH):
            initialize_database()
            
        with open(CSV_FILE_PATH, 'a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow([timestamp, f"{lat:.6f}", f"{lon:.6f}", str(signal)])
            f.flush()

def get_all_database_rows():
    """Reads and returns all target rows from the dedicated CSV database."""
    global CSV_FILE_PATH
    with db_lock:
        rows = []
        if os.path.exists(CSV_FILE_PATH):
            try:
                with open(CSV_FILE_PATH, 'r', encoding='utf-8') as f:
                    reader = csv.reader(f)
                    header = next(reader, None)  # Skip header
                    for row in reader:
                        if len(row) >= 4:
                            try:
                                rows.append({
                                    "timestamp": row[0],
                                    "latitude": float(row[1]),
                                    "longitude": float(row[2]),
                                    "signal": int(row[3])
                                })
                            except (ValueError, IndexError):
                                continue
            except Exception as e:
                print(f"Error reading CSV database: {e}")
        return rows

def delete_database_row(timestamp):
    """Deletes a target row from the dedicated CSV database by timestamp."""
    global CSV_FILE_PATH
    with db_lock:
        if os.path.exists(CSV_FILE_PATH):
            temp_rows = []
            try:
                with open(CSV_FILE_PATH, 'r', encoding='utf-8') as f:
                    reader = csv.reader(f)
                    header = next(reader, None)
                    for row in reader:
                        if row and row[0] != timestamp:
                            temp_rows.append(row)
                with open(CSV_FILE_PATH, 'w', newline='', encoding='utf-8') as f:
                    writer = csv.writer(f)
                    if header:
                        writer.writerow(header)
                    else:
                        writer.writerow(["Timestamp", "Latitude", "Longitude", "Signal"])
                    writer.writerows(temp_rows)
                    f.flush()
            except Exception as e:
                print(f"Error deleting row from CSV: {e}")

def add_log_entry(message):
    """Adds a telemetry log entry with timestamp."""
    timestamp = datetime.now().strftime("%H:%M:%S")
    hmi_state["logs"].append(f"[{timestamp}] {message}")
    if len(hmi_state["logs"]) > 50:
        hmi_state["logs"].pop(0)

def parse_serial_data(data):
    """Parses incoming raw serial lines and updates hmi_state."""
    data = data.strip()
    if not data:
        return
    
    add_log_entry(f"RAW RX: {data}")
    
    # Format 1: Metal Detection Trigger (d,lat,lon,strength)
    if data.startswith("d,"):
        parts = data.split(",")
        if len(parts) >= 3:
            try:
                lat = float(parts[1])
                lon = float(parts[2])
                signal = int(parts[3]) if len(parts) >= 4 else random.randint(70, 99)
                signal = max(0, min(100, signal))
                add_database_row(lat, lon, signal)
                add_log_entry(f"MET DETECTED: Lat {lat:.6f}, Lon {lon:.6f}, Signal {signal}%")
            except ValueError:
                add_log_entry("PARSE ERROR: Invalid Format 1 values")
                
    # Format 2: Full Hardware Telemetry Status [x,lat,lon,heading,battery,tiltx,tilty,speed,sensitivity]
    elif data.startswith("[x,") and data.endswith("]"):
        payload = data[3:-1]
        parts = payload.split(",")
        if len(parts) >= 8:
            try:
                hmi_state["rover_lat"] = float(parts[0])
                hmi_state["rover_lon"] = float(parts[1])
                hmi_state["heading"] = int(float(parts[2])) % 360
                hmi_state["battery"] = max(0, min(100, int(float(parts[3]))))
                hmi_state["tilt_x"] = int(float(parts[4]))
                hmi_state["tilt_y"] = int(float(parts[5]))
                hmi_state["speed"] = max(0, min(100, int(float(parts[6]))))
                hmi_state["sensitivity"] = max(0, min(100, int(float(parts[7]))))
                hmi_state["last_update"] = time.time()
                add_log_entry(f"TELEMETRY UP: Lat {hmi_state['rover_lat']:.6f}, Battery {hmi_state['battery']}%")
            except ValueError:
                add_log_entry("PARSE ERROR: Invalid Format 2 values")
                
    # Format 3: Shorthand Quick Parameters <speed,sensitivity>
    elif data.startswith("<") and data.endswith(">"):
        payload = data[1:-1]
        parts = payload.split(",")
        if len(parts) >= 2:
            try:
                hmi_state["speed"] = int(parts[0])
                hmi_state["sensitivity"] = int(parts[1])
                add_log_entry(f"SPEED/SENS UP: Speed {hmi_state['speed']}%, Sens {hmi_state['sensitivity']}%")
            except ValueError:
                add_log_entry("PARSE ERROR: Invalid Format 3 values")

# Physical Serial Connection Core
def connect_to_serial(port_name, baudrate):
    """Attempts to connect to a real serial port."""
    global serial_conn, hmi_state
    
    if not port_name:
        return {"status": "error", "message": "No serial port specified"}
    
    try:
        import serial
        serial_conn = serial.Serial(port_name, baudrate, timeout=0.1)
        hmi_state["connected"] = True
        hmi_state["port"] = port_name
        add_log_entry(f"Connected to Serial: {port_name} @ {baudrate}")
        
        # Start a thread to read the serial port
        def read_serial_worker():
            global serial_conn
            while hmi_state["connected"] and serial_conn and serial_conn.is_open:
                try:
                    if serial_conn.in_waiting:
                        line = serial_conn.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            parse_serial_data(line)
                except Exception as e:
                    add_log_entry(f"Serial read error: {e}")
                    break
                time.sleep(0.05)
            hmi_state["connected"] = False
            hmi_state["port"] = None
            
        t = threading.Thread(target=read_serial_worker, daemon=True)
        t.start()
        return {"status": "success", "connected": True, "port": port_name}
    except Exception as e:
        hmi_state["connected"] = False
        hmi_state["port"] = None
        add_log_entry(f"Connection failed to {port_name}: {e}")
        return {"status": "error", "connected": False, "message": str(e)}

def disconnect_serial():
    """Closes serial connection."""
    global serial_conn, hmi_state
    hmi_state["connected"] = False
    hmi_state["port"] = None
    if serial_conn and serial_conn.is_open:
        try:
            serial_conn.close()
        except:
            pass
    serial_conn = None
    add_log_entry("Disconnected serial line.")
    return {"status": "success", "connected": False}

def list_serial_ports():
    """Lists available COM/TTY ports."""
    ports = []
    try:
        import serial.tools.list_ports
        system_ports = list(serial.tools.list_ports.comports())
        for p in system_ports:
            ports.append(p.device)
    except Exception:
        pass
    return ports

def write_serial_command(command):
    """Writes a raw command string back to the serial interface."""
    global serial_conn, hmi_state
    add_log_entry(f"TX COMMAND: {command}")
    
    if command == "START":
        hmi_state["autonomous"] = True
        add_log_entry("ROVER STATE: Autonomous Mode Active")
    elif command == "STOP":
        hmi_state["autonomous"] = False
        add_log_entry("ROVER STATE: Autonomous Mode Suspended")
    elif command.startswith("BOUNDS="):
        add_log_entry(f"BOUNDARY SET: {command[7:]}")
    elif command.startswith("SET_SENS="):
        try:
            val = int(command.split("=")[1])
            hmi_state["sensitivity"] = val
            add_log_entry(f"ROVER STATE: Sensitivity updated to {val}%")
        except:
            pass
    
    if serial_conn and serial_conn.is_open:
        try:
            serial_conn.write((command + "\n").encode('utf-8'))
            return {"status": "success"}
        except Exception as e:
            return {"status": "error", "message": str(e)}
            
    return {"status": "success", "message": "Command logged (no active serial hardware)"}
