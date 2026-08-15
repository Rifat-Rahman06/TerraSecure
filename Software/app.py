import os
import io
import csv
import random
from flask import Flask, render_template, request, jsonify, Response, send_file
from server.utilities import (
    initialize_database,
    get_all_database_rows,
    add_database_row,
    delete_database_row,
    list_serial_ports,
    connect_to_serial,
    disconnect_serial,
    write_serial_command,
    hmi_state,
    CSV_FILE_PATH,
    DATA_DIR
)

app = Flask(__name__, template_folder='templates', static_folder='static')

# Initialize the database at application start
initialize_database()

# Route: Serve Main Index Page
@app.route('/')
def index():
    return render_template('index.html')

# API Route: Get complete status of HMI and detected targets
@app.route('/api/telemetry', methods=['GET'])
def get_telemetry():
    targets = get_all_database_rows()
    return jsonify({
        "status": "success",
        "state": hmi_state,
        "targets": targets
    })

# API Route: Get available serial ports list
@app.route('/api/serial/ports', methods=['GET'])
def get_serial_ports():
    ports = list_serial_ports()
    return jsonify({
        "status": "success",
        "ports": ports
    })

# API Route: Connect to a serial port
@app.route('/api/serial/connect', methods=['POST'])
def connect_serial_port():
    data = request.get_json() or {}
    port = data.get("port", "")
    baudrate = int(data.get("baudrate", 115200))
    
    result = connect_to_serial(port, baudrate)
    return jsonify(result)

# API Route: Disconnect from current serial port
@app.route('/api/serial/disconnect', methods=['POST'])
def disconnect_serial_port():
    result = disconnect_serial()
    return jsonify(result)

# API Route: Write custom command down serial line
@app.route('/api/serial/write', methods=['POST'])
def write_serial_cmd():
    data = request.get_json() or {}
    command = data.get("command", "")
    if not command:
        return jsonify({"status": "error", "message": "Empty command"}), 400
    
    result = write_serial_command(command)
    return jsonify(result)

# API Route: Toggle autonomous mode (START/STOP)
@app.route('/api/autonomous/toggle', methods=['POST'])
def toggle_autonomous():
    data = request.get_json() or {}
    active = data.get("active", False)
    
    cmd = "START" if active else "STOP"
    result = write_serial_command(cmd)
    return jsonify(result)

# API Route: Fetch targets list directly
@app.route('/api/targets', methods=['GET'])
def get_targets():
    rows = get_all_database_rows()
    return jsonify(rows)

# API Route: Add target manually or via Map Click
@app.route('/api/targets', methods=['POST'])
def add_target():
    data = request.get_json() or {}
    try:
        lat = float(data.get("latitude"))
        lon = float(data.get("longitude"))
        signal = int(data.get("signal", 90))
        add_database_row(lat, lon, signal)
        return jsonify({"status": "success", "message": "Target stored successfully"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400

# API Route: Delete target by timestamp (supports bulk deletion)
@app.route('/api/targets', methods=['DELETE'])
def delete_target():
    data = request.get_json() or {}
    timestamp = data.get("timestamp")
    timestamps = data.get("timestamps")
    
    if not timestamp and not timestamps:
        return jsonify({"status": "error", "message": "Timestamp or timestamps list required"}), 400
    
    if timestamps:
        deleted_count = 0
        for ts in timestamps:
            delete_database_row(ts)
            deleted_count += 1
        return jsonify({"status": "success", "message": f"Successfully deleted {deleted_count} targets."})
    else:
        delete_database_row(timestamp)
        return jsonify({"status": "success", "message": f"Target with timestamp {timestamp} deleted"})

# API Route: Download targets database as CSV (a direct copy of the live data/targets.csv file)
@app.route('/api/targets/download', methods=['GET'])
def download_targets_csv():
    if not os.path.exists(CSV_FILE_PATH):
        initialize_database()
    
    response = send_file(
        CSV_FILE_PATH,
        mimetype="text/csv",
        as_attachment=True,
        download_name="tersecure_detections.csv"
    )
    # Prevent browser caching so the user always downloads the latest updated data
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

# API Route: Upload CSV to load target detections
@app.route('/api/targets/upload', methods=['POST'])
def upload_targets_csv():
    if 'file' not in request.files:
        return jsonify({"status": "error", "message": "No file parameter specified"}), 400
    
    file = request.files['file']
    if file.filename == '':
        return jsonify({"status": "error", "message": "No file selected"}), 400
    
    if not file.filename.endswith('.csv'):
        return jsonify({"status": "error", "message": "Only CSV files are supported"}), 400
    
    try:
        # Read the file contents as text
        stream = io.StringIO(file.stream.read().decode("utf-8"), newline=None)
        reader = csv.reader(stream)
        
        # Parse the headers to find key columns
        header = next(reader, None)
        col_indices = {"timestamp": -1, "latitude": -1, "longitude": -1, "signal": -1}
        
        if header:
            is_header = False
            for idx, col in enumerate(header):
                col_lower = col.lower().strip()
                if "lat" in col_lower:
                    col_indices["latitude"] = idx
                    is_header = True
                elif "lon" in col_lower:
                    col_indices["longitude"] = idx
                    is_header = True
                elif "time" in col_lower:
                    col_indices["timestamp"] = idx
                    is_header = True
                elif "sig" in col_lower or "detect" in col_lower:
                    col_indices["signal"] = idx
                    is_header = True
            
            # If we don't find a valid coordinate column, treat header row as first data row
            if not is_header or col_indices["latitude"] == -1 or col_indices["longitude"] == -1:
                stream.seek(0)
                reader = csv.reader(stream)
                col_indices = {"timestamp": 0, "latitude": 1, "longitude": 2, "signal": 3}
        else:
            col_indices = {"timestamp": 0, "latitude": 1, "longitude": 2, "signal": 3}
            
        success_count = 0
        for row in reader:
            if not row or len(row) == 0:
                continue
                
            try:
                lat_idx = col_indices["latitude"]
                lon_idx = col_indices["longitude"]
                sig_idx = col_indices["signal"]
                time_idx = col_indices["timestamp"]
                
                # Extract coordinates
                if lat_idx >= len(row) or lon_idx >= len(row):
                    continue
                lat = float(row[lat_idx])
                lon = float(row[lon_idx])
                
                # Check lat/lon validity
                if not (-90 <= lat <= 90) or not (-180 <= lon <= 180):
                    continue
                
                # Extract optional signal level
                signal = 90
                if sig_idx != -1 and sig_idx < len(row):
                    try:
                        # Clean signal from "%" symbols or other characters
                        sig_clean = row[sig_idx].replace("%", "").strip()
                        signal = int(sig_clean)
                    except ValueError:
                        pass
                else:
                    signal = random.randint(75, 96)
                    
                # Extract optional timestamp
                timestamp = None
                if time_idx != -1 and time_idx < len(row) and row[time_idx].strip():
                    timestamp = row[time_idx].strip()
                
                # Write to database
                add_database_row(lat, lon, signal, timestamp=timestamp)
                success_count += 1
            except (ValueError, IndexError):
                continue
                
        return jsonify({
            "status": "success",
            "message": f"Successfully loaded {success_count} target detections from CSV.",
            "targets": get_all_database_rows()
        })
        
    except Exception as e:
        return jsonify({"status": "error", "message": f"Failed to parse uploaded CSV: {str(e)}"}), 400

# API Route: Set 4-Corner Rectangular Rover Boundary
@app.route('/api/boundary/set', methods=['POST'])
def set_rover_boundary():
    data = request.get_json() or {}
    corners = data.get("corners", {})
    # Format: NW, NE, SE, SW
    nw = corners.get("nw")
    ne = corners.get("ne")
    se = corners.get("se")
    sw = corners.get("sw")
    
    if not (nw and ne and se and sw):
        return jsonify({"status": "error", "message": "All 4 corner coordinates (NW, NE, SE, SW) are required"}), 400
        
    cmd = f"BOUNDS={nw[0]:.6f},{nw[1]:.6f};{ne[0]:.6f},{ne[1]:.6f};{se[0]:.6f},{se[1]:.6f};{sw[0]:.6f},{sw[1]:.6f}"
    result = write_serial_command(cmd)
    return jsonify({
        "status": "success",
        "serial_result": result,
        "command": cmd,
        "message": "Boundary coordinates transmitted to Rover controller"
    })

# Start application server
if __name__ == '__main__':
    # Run server on port 3000
    app.run(host='0.0.0.0', port=3000, debug=True, use_reloader=False)
