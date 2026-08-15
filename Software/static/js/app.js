/**
 * THE MINER HMI - ROVER COMMAND CENTER
 * Refactored modular vanilla JS engine managing telemetry, detection logs,
 * fixed-width metrics, 4-point normalized rectangular boundary mode, and serial interaction.
 */

document.addEventListener('DOMContentLoaded', function() {
    // --- Application State Managers ---
    let map = null;
    let roverMarker = null;
    let roverPath = null;
    let pathCoordinates = [];
    let targetMarkers = {}; // Keyed by target timestamp
    let selectedTargetTimestamp = null;
    let markedTargetTimestamps = new Set();
    let lastLoadedLogCount = 0;
    
    // --- Boundary Mode State (The ONLY Map Interaction Mode) ---
    let isBoundaryMode = false;
    let isAutonomousActive = false;
    let boundaryClickPoints = [];
    let boundaryClickMarkers = [];
    let boundaryPolygon = null;
    let boundaryCornerMarkers = [];
    let normalizedBoundaryCorners = null;

    // --- Custom Map Icons ---
    const roverIcon = L.divIcon({
        className: 'custom-rover-marker',
        html: `
            <span class="rover-pulse-ring"></span>
            <div style="background-color: #2563eb; width: 18px; height: 18px; border: 3px solid #ffffff; border-radius: 50%; box-shadow: 0 0 8px #2563eb; position: relative; z-index: 2;"></div>`,
        iconSize: [18, 18],
        iconAnchor: [9, 9]
    });

    const targetIcon = L.divIcon({
        className: 'custom-target-marker',
        html: `<div style="background-color: #f59e0b; width: 14px; height: 14px; border: 2px solid #ffffff; border-radius: 50%; box-shadow: 0 0 6px #f59e0b;"></div>`,
        iconSize: [14, 14],
        iconAnchor: [7, 7]
    });

    // Helper: Create custom labeled corner marker
    function createCornerIcon(label, isPointNumber = false) {
        const bg = isPointNumber ? '#475569' : '#2563eb';
        return L.divIcon({
            className: 'boundary-corner-marker-wrapper',
            html: `<div style="background-color: ${bg}; color: #ffffff; font-size: 9px; font-weight: 800; border: 2px solid #ffffff; border-radius: 50%; width: 22px; height: 22px; display: flex; align-items: center; justify-content: center; box-shadow: 0 2px 6px rgba(0,0,0,0.3);">${label}</div>`,
            iconSize: [22, 22],
            iconAnchor: [11, 11]
        });
    }

    // --- DOM Elements Cache ---
    const elements = {
        // Serial Status (Strictly Connected / Disconnected)
        serialStatusDot: document.getElementById('serialStatusDot'),
        serialStatusText: document.getElementById('serialStatusText'),
        serialPortSelect: document.getElementById('serialPortSelect'),
        baudRateSelect: document.getElementById('baudRateSelect'),
        connectSerialBtn: document.getElementById('connectSerialBtn'),
        disconnectSerialBtn: document.getElementById('disconnectSerialBtn'),
        
        // Navigation & Global Actions
        openSettingsBtn: document.getElementById('openSettingsBtn'),
        closeSettingsModalBtn: document.getElementById('closeSettingsModalBtn'),
        settingsModal: document.getElementById('settingsModal'),
        fullScreenBtn: document.getElementById('fullScreenBtn'),
        hmiRoot: document.getElementById('hmiRoot'),
        
        // Autonomous Controls
        autoToggleBtn: document.getElementById('autoToggleBtn'),

        // Detection Log (Left Panel)
        targetCountBadge: document.getElementById('targetCountBadge'),
        targetsList: document.getElementById('targetsList'),
        targetsEmptyState: document.getElementById('targetsEmptyState'),
        markAllTargets: document.getElementById('markAllTargets'),
        deleteTargetBtn: document.getElementById('deleteTargetBtn'),
        downloadCsvBtn: document.getElementById('downloadCsvBtn'),
        uploadCsvTriggerBtn: document.getElementById('uploadCsvTriggerBtn'),
        csvFileInput: document.getElementById('csvFileInput'),
        targetsListContainer: document.querySelector('.targets-list-container'),
        
        // Map Telemetry & Tilt Panels (Fixed-Width)
        pitchVal: document.getElementById('pitchVal'),
        rollVal: document.getElementById('rollVal'),
        mapHoverCoords: document.getElementById('mapHoverCoords'),
        attitudeBubble: document.getElementById('attitudeBubble'),

        // Boundary Floating Overlay Modal
        boundaryOverlayCard: document.getElementById('boundaryOverlayCard'),
        boundaryModalTitle: document.getElementById('boundaryModalTitle'),
        cancelBoundaryBtn: document.getElementById('cancelBoundaryBtn'),
        boundaryInstructionText: document.getElementById('boundaryInstructionText'),
        boundaryCoordsGrid: document.getElementById('boundaryCoordsGrid'),
        coordNW: document.getElementById('coordNW'),
        coordNE: document.getElementById('coordNE'),
        coordSE: document.getElementById('coordSE'),
        coordSW: document.getElementById('coordSW'),
        resetBoundaryBtn: document.getElementById('resetBoundaryBtn'),
        startBoundaryAutoBtn: document.getElementById('startBoundaryAutoBtn'),
        startBoundaryBtnText: document.getElementById('startBoundaryBtnText'),

        // Map Overlays: Rover Stats, Compass, and Terminal Output Stream
        batteryText: document.getElementById('batteryText'),
        speedText: document.getElementById('speedText'),
        sensitivityText: document.getElementById('sensitivityText'),
        compassDial: document.getElementById('compassDial'),
        compassText: document.getElementById('compassText'),
        mapTerminalOverlay: document.getElementById('mapTerminalOverlay'),
        terminalToggleBtn: document.getElementById('terminalToggleBtn'),
        terminalToggleIcon: document.getElementById('terminalToggleIcon'),
        consoleLogBox: document.getElementById('consoleLogBox')
    };

    // --- Helper: Format Timestamp to 12-Hour AM/PM (No Seconds, e.g. 02:35 PM) ---
    function formatTimestamp12Hour(rawTs) {
        if (!rawTs) return "--:-- --";
        try {
            // Check if string contains standard datetime
            let dateObj = null;
            if (typeof rawTs === 'string' && (rawTs.includes(" ") || rawTs.includes("T"))) {
                dateObj = new Date(rawTs.replace(" ", "T"));
            }
            
            if (dateObj && !isNaN(dateObj.getTime())) {
                let hours = dateObj.getHours();
                const minutes = dateObj.getMinutes();
                const ampm = hours >= 12 ? 'PM' : 'AM';
                hours = hours % 12;
                hours = hours ? hours : 12; // 0 becomes 12
                const hStr = hours < 10 ? '0' + hours : '' + hours;
                const mStr = minutes < 10 ? '0' + minutes : '' + minutes;
                return `${hStr}:${mStr} ${ampm}`;
            }

            // Fallback for time-only string e.g. "14:35:22" or "02:35:10 PM"
            if (typeof rawTs === 'string' && (rawTs.includes("AM") || rawTs.includes("PM"))) {
                const parts = rawTs.trim().split(/\s+/);
                const timePart = parts[0];
                const ampmPart = parts[1] || "";
                const subParts = timePart.split(":");
                if (subParts.length >= 2) {
                    let hours = parseInt(subParts[0], 10);
                    if (!isNaN(hours)) {
                        hours = hours % 12;
                        hours = hours ? hours : 12;
                        const hStr = hours < 10 ? '0' + hours : '' + hours;
                        return `${hStr}:${subParts[1]} ${ampmPart}`.trim();
                    }
                }
                return rawTs;
            }

            if (typeof rawTs === 'string' && rawTs.includes(":")) {
                const timeParts = rawTs.split(":");
                if (timeParts.length >= 2) {
                    let hours = parseInt(timeParts[0], 10);
                    const minutes = timeParts[1].slice(0, 2);
                    if (!isNaN(hours)) {
                        const ampm = hours >= 12 ? 'PM' : 'AM';
                        hours = hours % 12;
                        hours = hours ? hours : 12;
                        const hStr = hours < 10 ? '0' + hours : '' + hours;
                        return `${hStr}:${minutes} ${ampm}`;
                    }
                }
            }

            return rawTs;
        } catch (e) {
            return rawTs;
        }
    }

    // --- Initialization Core ---
    function initialize() {
        initMap();
        initEventListeners();
        fetchSerialPorts();
        
        // Live polling loop
        pollTelemetry();
        setInterval(pollTelemetry, 1000);

        // Render Lucide icons
        if (window.lucide) {
            lucide.createIcons();
        }
    }

    // --- Leaflet Map Setup ---
    function initMap() {
        map = L.map('map', {
            center: [23.8, 90.35],
            zoom: 16,
            zoomControl: true,
            attributionControl: false
        });

        // Clean street tile layer
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            maxZoom: 19
        }).addTo(map);

        // Rover path polyline
        roverPath = L.polyline([], {
            color: '#2563eb',
            weight: 4,
            opacity: 0.85,
            dashArray: '1, 5'
        }).addTo(map);

        // Map Click Handling: Boundary Mode is the ONLY interaction mode
        map.on('click', function(e) {
            if (isBoundaryMode) {
                handleBoundaryMapClick(e.latlng);
            }
        });

        // Map hover coordinates feedback
        map.on('mousemove', function(e) {
            if (elements.mapHoverCoords) {
                elements.mapHoverCoords.textContent = `Lat: ${e.latlng.lat.toFixed(6)} | Long: ${e.latlng.lng.toFixed(6)}`;
            }
        });
    }

    // --- 4-Point Boundary & Autonomous Workflow ---
    function openBoundaryModal() {
        if (isAutonomousActive) {
            // Autonomous operation is running: show active state and option to stop
            if (elements.boundaryModalTitle) elements.boundaryModalTitle.textContent = "AUTONOMOUS OPERATION ACTIVE";
            if (elements.boundaryInstructionText) {
                elements.boundaryInstructionText.textContent = "Rover is operating in autonomous mode within the defined boundary. Click below to stop autonomous operation.";
            }
            if (normalizedBoundaryCorners && elements.boundaryCoordsGrid) {
                elements.boundaryCoordsGrid.style.display = 'grid';
            }
            if (elements.resetBoundaryBtn) elements.resetBoundaryBtn.style.display = 'none';
            if (elements.startBoundaryAutoBtn) {
                elements.startBoundaryAutoBtn.style.display = 'inline-flex';
                elements.startBoundaryAutoBtn.className = 'btn btn-compact btn-danger-action';
                elements.startBoundaryAutoBtn.innerHTML = `<i data-lucide="square"></i><span>Stop Autonomous</span>`;
            }
        } else {
            // Autonomous operation is inactive: configure or start boundary
            if (elements.boundaryModalTitle) elements.boundaryModalTitle.textContent = "4-POINT AUTONOMOUS BOUNDARY";
            isBoundaryMode = true;
            
            if (normalizedBoundaryCorners) {
                if (elements.boundaryInstructionText) {
                    elements.boundaryInstructionText.textContent = "Boundary defined. Click Start Autonomous to begin operation.";
                }
                if (elements.boundaryCoordsGrid) elements.boundaryCoordsGrid.style.display = 'grid';
                if (elements.resetBoundaryBtn) elements.resetBoundaryBtn.style.display = 'inline-flex';
                if (elements.startBoundaryAutoBtn) {
                    elements.startBoundaryAutoBtn.style.display = 'inline-flex';
                    elements.startBoundaryAutoBtn.className = 'btn btn-compact btn-success-action';
                    elements.startBoundaryAutoBtn.innerHTML = `<i data-lucide="play"></i><span>Start Autonomous</span>`;
                }
            } else {
                if (elements.boundaryInstructionText) {
                    elements.boundaryInstructionText.textContent = `Click 4 points on the map to define the boundary corners (${boundaryClickPoints.length}/4)`;
                }
                if (elements.boundaryCoordsGrid) elements.boundaryCoordsGrid.style.display = 'none';
                if (elements.resetBoundaryBtn) {
                    elements.resetBoundaryBtn.style.display = boundaryClickPoints.length > 0 ? 'inline-flex' : 'none';
                }
                if (elements.startBoundaryAutoBtn) elements.startBoundaryAutoBtn.style.display = 'none';
            }
        }
        
        elements.boundaryOverlayCard.style.display = 'block';
        appendConsoleLine("SYSTEM MODE: Autonomous Boundary configuration modal opened.", "log");
        if (window.lucide) lucide.createIcons();
    }

    function closeBoundaryModal() {
        elements.boundaryOverlayCard.style.display = 'none';
        isBoundaryMode = false;
        appendConsoleLine("SYSTEM MODE: Autonomous modal closed.", "log");
    }

    async function resetBoundaryData() {
        // If autonomous mode is active, turn it off immediately
        if (isAutonomousActive) {
            await toggleAutonomousMode(false);
        }

        boundaryClickPoints = [];
        // Remove click markers
        boundaryClickMarkers.forEach(m => m.remove());
        boundaryClickMarkers = [];
        // Remove polygon
        if (boundaryPolygon) {
            boundaryPolygon.remove();
            boundaryPolygon = null;
        }
        // Remove corner markers
        boundaryCornerMarkers.forEach(m => m.remove());
        boundaryCornerMarkers = [];
        normalizedBoundaryCorners = null;
        
        isBoundaryMode = true;
        if (elements.boundaryInstructionText) {
            elements.boundaryInstructionText.textContent = `Click 4 points on the map to define the boundary corners (0/4)`;
        }
        if (elements.boundaryCoordsGrid) elements.boundaryCoordsGrid.style.display = 'none';
        if (elements.resetBoundaryBtn) elements.resetBoundaryBtn.style.display = 'none';
        if (elements.startBoundaryAutoBtn) elements.startBoundaryAutoBtn.style.display = 'none';
        appendConsoleLine("SYSTEM MODE: Boundary points reset. Click 4 points on the map.", "log");
        if (window.lucide) lucide.createIcons();
    }

    function handleBoundaryMapClick(latlng) {
        if (boundaryClickPoints.length >= 4) {
            // Already created, ignore extra clicks unless reset
            return;
        }

        boundaryClickPoints.push(latlng);
        const pointNum = boundaryClickPoints.length;

        // Add visual numbered pin for this click
        const clickMarker = L.marker([latlng.lat, latlng.lng], {
            icon: createCornerIcon(`P${pointNum}`, true)
        }).addTo(map);
        boundaryClickMarkers.push(clickMarker);

        appendConsoleLine(`BOUNDARY CLICK ${pointNum}/4: [${latlng.lat.toFixed(6)}, ${latlng.lng.toFixed(6)}]`, "tx");
        if (elements.boundaryInstructionText) {
            elements.boundaryInstructionText.textContent = `Click 4 points on the map to define the boundary corners (${pointNum}/4)`;
        }
        if (elements.resetBoundaryBtn) {
            elements.resetBoundaryBtn.style.display = 'inline-flex';
        }

        // When 4th point is clicked: calculate and normalize into a proper rectangle!
        if (boundaryClickPoints.length === 4) {
            normalizeAndDrawRectangle();
        }
    }

    function normalizeAndDrawRectangle() {
        // Calculate the minimum axis-aligned bounding rectangle covering all 4 points
        const lats = boundaryClickPoints.map(p => p.lat);
        const lngs = boundaryClickPoints.map(p => p.lng);

        const minLat = Math.min(...lats);
        const maxLat = Math.max(...lats);
        const minLon = Math.min(...lngs);
        const maxLon = Math.max(...lngs);

        // 4 exact rectangular corners (strictly equal opposite sides and parallel alignment):
        const nw = [maxLat, minLon];
        const ne = [maxLat, maxLon];
        const se = [minLat, maxLon];
        const sw = [minLat, minLon];

        normalizedBoundaryCorners = { nw, ne, se, sw };

        // Clean up preliminary click markers
        boundaryClickMarkers.forEach(m => m.remove());
        boundaryClickMarkers = [];

        // Draw the normalized rectangle on the map
        const rectCoords = [nw, ne, se, sw];
        if (boundaryPolygon) boundaryPolygon.remove();
        boundaryPolygon = L.polygon(rectCoords, {
            color: '#2563eb',
            weight: 2.5,
            fillColor: '#3b82f6',
            fillOpacity: 0.18,
            dashArray: '4, 4'
        }).addTo(map);

        // Place clear corner markers (NW, NE, SE, SW)
        const cornerDefinitions = [
            { label: 'NW', pos: nw },
            { label: 'NE', pos: ne },
            { label: 'SE', pos: se },
            { label: 'SW', pos: sw }
        ];

        cornerDefinitions.forEach(c => {
            const m = L.marker(c.pos, {
                icon: createCornerIcon(c.label)
            }).addTo(map).bindPopup(`<b>${c.label} Corner</b><br>Lat: ${c.pos[0].toFixed(6)}<br>Lon: ${c.pos[1].toFixed(6)}`);
            boundaryCornerMarkers.push(m);
        });

        // Fit map bounds to show full rectangle with breathing room
        map.fitBounds(boundaryPolygon.getBounds(), { padding: [40, 40] });

        // Update UI info card with normalized corner coordinates
        if (elements.coordNW) elements.coordNW.textContent = `${nw[0].toFixed(5)}, ${nw[1].toFixed(5)}`;
        if (elements.coordNE) elements.coordNE.textContent = `${ne[0].toFixed(5)}, ${ne[1].toFixed(5)}`;
        if (elements.coordSE) elements.coordSE.textContent = `${se[0].toFixed(5)}, ${se[1].toFixed(5)}`;
        if (elements.coordSW) elements.coordSW.textContent = `${sw[0].toFixed(5)}, ${sw[1].toFixed(5)}`;

        if (elements.boundaryInstructionText) {
            elements.boundaryInstructionText.textContent = "Boundary locked. Click Start Autonomous to begin operation.";
        }
        if (elements.boundaryCoordsGrid) elements.boundaryCoordsGrid.style.display = 'grid';
        if (elements.resetBoundaryBtn) elements.resetBoundaryBtn.style.display = 'inline-flex';
        if (elements.startBoundaryAutoBtn) {
            elements.startBoundaryAutoBtn.style.display = 'inline-flex';
            elements.startBoundaryAutoBtn.className = 'btn btn-compact btn-success-action';
            elements.startBoundaryAutoBtn.innerHTML = `<i data-lucide="play"></i><span>Start Autonomous</span>`;
        }

        appendConsoleLine(`NORMALIZED RECTANGLE GENERATED: NW(${nw[0].toFixed(6)}, ${nw[1].toFixed(6)}) to SE(${se[0].toFixed(6)}, ${se[1].toFixed(6)})`, "rx");
        if (window.lucide) lucide.createIcons();
    }

    async function submitBoundaryAndStartAutonomous() {
        if (!normalizedBoundaryCorners) {
            appendConsoleLine("FAIL: Please set 4 boundary points first.", "err");
            return;
        }

        appendConsoleLine("TX: Transmitting 4-corner boundary coordinates to Rover...", "tx");
        try {
            // 1. Send boundary to backend / serial
            const response = await fetch('/api/boundary/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ corners: normalizedBoundaryCorners })
            });
            const data = await response.json();
            if (data.status === 'success') {
                appendConsoleLine(`BOUNDS ACK: ${data.message}`, "rx");
            }

            // 2. Trigger autonomous mode
            await toggleAutonomousMode(true);

            // 3. Automatically close modal after starting
            closeBoundaryModal();
        } catch (err) {
            appendConsoleLine(`FAIL: Boundary dispatch failed: ${err.message}`, "err");
        }
    }

    // --- Serial Communication Handlers ---
    async function toggleSerialConnection(connect) {
        if (connect) {
            const port = elements.serialPortSelect.value;
            const baudrate = elements.baudRateSelect.value;
            if (!port) {
                appendConsoleLine("FAIL: Please select a valid serial port.", "err");
                return;
            }
            appendConsoleLine(`CONNECTING: Opening ${port} @ ${baudrate}...`, "log");
            
            try {
                const response = await fetch('/api/serial/connect', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ port: port, baudrate: baudrate })
                });
                const data = await response.json();
                if (data.connected) {
                    appendConsoleLine(`CONNECTED: Successfully opened ${data.port}.`, "rx");
                } else {
                    appendConsoleLine(`FAIL: Could not open port: ${data.message || 'Check connection'}`, "err");
                }
            } catch (err) {
                appendConsoleLine(`FAIL: Serial network connection error.`, "err");
            }
        } else {
            appendConsoleLine(`DISCONNECTING: Closing serial line...`, "log");
            try {
                const response = await fetch('/api/serial/disconnect', { method: 'POST' });
                const data = await response.json();
                if (!data.connected) {
                    appendConsoleLine(`DISCONNECTED: Serial port closed.`, "rx");
                }
            } catch (err) {
                appendConsoleLine(`FAIL: Serial disconnection error.`, "err");
            }
        }
    }

    // Toggles the autonomous execution state
    async function toggleAutonomousMode(forceState = null) {
        const activate = forceState !== null ? forceState : !isAutonomousActive;
        
        appendConsoleLine(`TX: Setting Autonomous Mode to ${activate ? 'START' : 'STOP'}`, "tx");
        
        try {
            const response = await fetch('/api/autonomous/toggle', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ active: activate })
            });
            
            if (!response.ok) {
                throw new Error(`HTTP error ${response.status}`);
            }
            
            const data = await response.json();
            if (data.status === 'success') {
                updateAutoStateUI(activate);
                if (data.message) {
                    appendConsoleLine(`AUTO STATUS: ${data.message}`, "rx");
                }
                if (!activate) {
                    closeBoundaryModal();
                }
            } else {
                appendConsoleLine(`FAIL: Auto-toggle: ${data.message || 'Unknown error'}`, "err");
            }
        } catch (err) {
            console.error("Auto toggle error:", err);
            appendConsoleLine(`FAIL: Auto-toggle request failed: ${err.message}`, "err");
        }
    }

    function updateAutoStateUI(active) {
        isAutonomousActive = Boolean(active);
        if (elements.autoToggleBtn) {
            if (isAutonomousActive) {
                elements.autoToggleBtn.className = 'btn btn-auto-main active-auto';
                elements.autoToggleBtn.innerHTML = `<i data-lucide="square"></i><span>STOP AUTONOMOUS</span>`;
                elements.autoToggleBtn.title = "Autonomous Mode Active - Click to stop operation";
            } else {
                elements.autoToggleBtn.className = 'btn btn-primary btn-auto-main';
                elements.autoToggleBtn.innerHTML = `<i data-lucide="play"></i><span>START AUTONOMOUS</span>`;
                elements.autoToggleBtn.title = "Click to set 4-point boundary and start autonomous operation";
            }
        }
        if (elements.startBoundaryAutoBtn) {
            if (isAutonomousActive) {
                elements.startBoundaryAutoBtn.className = 'btn btn-compact btn-danger-action';
                elements.startBoundaryAutoBtn.innerHTML = `<i data-lucide="square"></i><span>Stop Autonomous</span>`;
            } else {
                elements.startBoundaryAutoBtn.className = 'btn btn-compact btn-success-action';
                elements.startBoundaryAutoBtn.innerHTML = `<i data-lucide="play"></i><span>Start Autonomous</span>`;
            }
        }
        if (window.lucide) lucide.createIcons();
    }

    // --- Telemetry Polling & Dashboard UI Updates ---
    async function pollTelemetry() {
        try {
            const response = await fetch('/api/telemetry');
            const data = await response.json();
            if (data.status === 'success') {
                updateDashboardUI(data.state);
                updateTargetsListUI(data.targets);
            }
        } catch (err) {
            elements.serialStatusDot.className = "status-dot disconnected";
            elements.serialStatusText.textContent = "DISCONNECTED";
        }
    }

    function updateDashboardUI(state) {
        // 1. Connection Status: Strictly Connected / Disconnected
        if (state.connected) {
            elements.serialStatusDot.className = "status-dot connected";
            elements.serialStatusText.textContent = "CONNECTED";
            elements.connectSerialBtn.disabled = true;
            elements.disconnectSerialBtn.disabled = false;
        } else {
            elements.serialStatusDot.className = "status-dot disconnected";
            elements.serialStatusText.textContent = "DISCONNECTED";
            elements.connectSerialBtn.disabled = false;
            elements.disconnectSerialBtn.disabled = true;
        }

        // 2. Map position tracking of the rover
        const lat = state.rover_lat;
        const lon = state.rover_lon;
        if (lat && lon) {
            const pos = [lat, lon];
            if (!roverMarker) {
                roverMarker = L.marker(pos, { icon: roverIcon }).addTo(map);
                map.setView(pos, 18);
            } else {
                roverMarker.setLatLng(pos);
            }

            const lastCoord = pathCoordinates[pathCoordinates.length - 1];
            if (!lastCoord || lastCoord[0] !== lat || lastCoord[1] !== lon) {
                pathCoordinates.push(pos);
                if (pathCoordinates.length > 100) pathCoordinates.shift();
                roverPath.setLatLngs(pathCoordinates);
            }
        }

        // 3. Map Overlay Stats: Battery, Speed, Sensitivity
        if (elements.batteryText) {
            elements.batteryText.textContent = `${state.battery.toFixed(0)}%`;
        }
        if (elements.speedText) {
            elements.speedText.textContent = `${state.speed}%`;
        }
        if (elements.sensitivityText) {
            elements.sensitivityText.textContent = `${state.sensitivity}%`;
        }

        // 4. Fixed-width Tilt X and Tilt Y Panels & Attitude Bubble
        if (elements.pitchVal) {
            elements.pitchVal.textContent = `${state.tilt_x}°`;
        }
        if (elements.rollVal) {
            elements.rollVal.textContent = `${state.tilt_y}°`;
        }
        if (elements.attitudeBubble) {
            const bubbleX = Math.max(-40, Math.min(40, state.tilt_x * 2.5));
            const bubbleY = Math.max(-40, Math.min(40, -state.tilt_y * 2.5));
            elements.attitudeBubble.style.transform = `translate(${bubbleX}px, ${bubbleY}px)`;
        }

        // 5. Compass Heading Overlay
        if (elements.compassDial) {
            elements.compassDial.style.transform = `rotate(${-state.heading}deg)`;
        }
        if (elements.compassText) {
            elements.compassText.textContent = `${state.heading}°`;
        }

        // 6. Autonomous Execution Status
        updateAutoStateUI(state.autonomous);

        // 7. Console Trace Logs
        if (state.logs && state.logs.length > lastLoadedLogCount) {
            for (let i = lastLoadedLogCount; i < state.logs.length; i++) {
                const rawLog = state.logs[i];
                let type = "log";
                if (rawLog.includes("RAW RX:")) type = "rx";
                if (rawLog.includes("TX COMMAND:")) type = "tx";
                if (rawLog.includes("FAIL:") || rawLog.includes("ERROR:")) type = "err";
                
                appendConsoleLine(rawLog, type);
            }
            lastLoadedLogCount = state.logs.length;
        }
    }

    function updateDeleteButtonState() {
        if (selectedTargetTimestamp !== null || markedTargetTimestamps.size > 0) {
            elements.deleteTargetBtn.disabled = false;
        } else {
            elements.deleteTargetBtn.disabled = true;
        }
    }

    // --- Detection Log Panel UI Updating ---
    function updateTargetsListUI(targets) {
        // Requirement 2: Display "Total: [count]" (no "Targets" label)
        elements.targetCountBadge.textContent = `Total: ${targets.length}`;
        
        if (targets.length > 0) {
            elements.targetsEmptyState.style.display = "none";
        } else {
            elements.targetsEmptyState.style.display = "flex";
            elements.targetsList.innerHTML = "";
            elements.targetsList.appendChild(elements.targetsEmptyState);
            Object.values(targetMarkers).forEach(marker => marker.remove());
            targetMarkers = {};
            elements.deleteTargetBtn.disabled = true;
            if (elements.markAllTargets) elements.markAllTargets.checked = false;
            markedTargetTimestamps.clear();
            return;
        }

        const activeTimestamps = new Set(targets.map(t => t.timestamp));
        
        // Remove old markers
        for (const [timestamp, marker] of Object.entries(targetMarkers)) {
            if (!activeTimestamps.has(timestamp)) {
                marker.remove();
                delete targetMarkers[timestamp];
            }
        }

        // Clean stale checkboxes
        for (const ts of markedTargetTimestamps) {
            if (!activeTimestamps.has(ts)) {
                markedTargetTimestamps.delete(ts);
            }
        }

        if (elements.markAllTargets) {
            const allChecked = targets.length > 0 && targets.every(t => markedTargetTimestamps.has(t.timestamp));
            elements.markAllTargets.checked = allChecked;
        }

        // Clear existing items and render fresh
        elements.targetsList.querySelectorAll('.target-item').forEach(el => el.remove());

        targets.forEach(target => {
            if (!targetMarkers[target.timestamp]) {
                const marker = L.marker([target.latitude, target.longitude], { icon: targetIcon })
                    .addTo(map)
                    .bindPopup(`<b>Detected Metal</b><br>Signal: ${target.signal}%<br>Lat: ${target.latitude.toFixed(6)}<br>Lon: ${target.longitude.toFixed(6)}`);
                targetMarkers[target.timestamp] = marker;
            }

            const item = document.createElement('li');
            item.className = `target-item ${selectedTargetTimestamp === target.timestamp ? 'selected' : ''}`;
            item.dataset.timestamp = target.timestamp;
            item.dataset.lat = target.latitude;
            item.dataset.lon = target.longitude;

            // Requirement 3: 12-hour AM/PM format (e.g. 02:35 PM, no seconds)
            const formattedTime = formatTimestamp12Hour(target.timestamp);
            const isChecked = markedTargetTimestamps.has(target.timestamp) ? 'checked' : '';

            item.innerHTML = `
                <div style="display: flex; align-items: center; justify-content: center; height: 100%;" onclick="event.stopPropagation();">
                    <input type="checkbox" class="target-checkbox" data-timestamp="${target.timestamp}" style="cursor: pointer; accent-color: var(--hmi-accent); width: 14px; height: 14px; margin: 0;" ${isChecked} />
                </div>
                <span class="target-time">${formattedTime}</span>
                <span class="target-coords">${target.latitude.toFixed(5)}, ${target.longitude.toFixed(5)}</span>
                <div class="target-signal-badge">${target.signal}%</div>
            `;

            const checkbox = item.querySelector('.target-checkbox');
            checkbox.addEventListener('change', function() {
                if (this.checked) {
                    markedTargetTimestamps.add(target.timestamp);
                } else {
                    markedTargetTimestamps.delete(target.timestamp);
                }
                
                if (elements.markAllTargets) {
                    const totalCheckboxes = elements.targetsList.querySelectorAll('.target-checkbox');
                    const checkedCheckboxes = elements.targetsList.querySelectorAll('.target-checkbox:checked');
                    elements.markAllTargets.checked = (totalCheckboxes.length > 0 && totalCheckboxes.length === checkedCheckboxes.length);
                }
                updateDeleteButtonState();
            });

            item.onclick = function() {
                elements.targetsList.querySelectorAll('.target-item').forEach(el => el.classList.remove('selected'));
                
                if (selectedTargetTimestamp === target.timestamp) {
                    selectedTargetTimestamp = null;
                } else {
                    selectedTargetTimestamp = target.timestamp;
                    item.classList.add('selected');

                    map.flyTo([target.latitude, target.longitude], 19, {
                        animate: true,
                        duration: 1.2
                    });
                    
                    if (targetMarkers[target.timestamp]) {
                        targetMarkers[target.timestamp].openPopup();
                    }
                }
                updateDeleteButtonState();
            };

            elements.targetsList.appendChild(item);
        });

        updateDeleteButtonState();
    }

    // --- Delete Target Action ---
    async function deleteSelectedTarget() {
        const toDelete = new Set();
        if (markedTargetTimestamps.size > 0) {
            markedTargetTimestamps.forEach(ts => toDelete.add(ts));
        } else if (selectedTargetTimestamp) {
            toDelete.add(selectedTargetTimestamp);
        }

        if (toDelete.size === 0) return;
        
        appendConsoleLine(`TX COMMAND: Deleting ${toDelete.size} detection(s)...`, "tx");

        try {
            const bodyPayload = toDelete.size > 1 || markedTargetTimestamps.size > 0
                ? { timestamps: Array.from(toDelete) }
                : { timestamp: Array.from(toDelete)[0] };

            const response = await fetch('/api/targets', {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(bodyPayload)
            });
            const data = await response.json();
            if (data.status === 'success') {
                appendConsoleLine(`SUCCESS: ${toDelete.size} detection(s) deleted from CSV.`, "rx");
                selectedTargetTimestamp = null;
                markedTargetTimestamps.clear();
                if (elements.markAllTargets) {
                    elements.markAllTargets.checked = false;
                }
                updateDeleteButtonState();
                pollTelemetry();
            }
        } catch (err) {
            appendConsoleLine(`FAIL: Deletion process failed: ${err.message}`, "err");
        }
    }

    // --- CSV Upload Handler ---
    async function handleCSVUpload(file) {
        appendConsoleLine(`SYSTEM: Uploading CSV detections file: "${file.name}"...`, "log");
        
        const formData = new FormData();
        formData.append('file', file);
        
        try {
            const response = await fetch('/api/targets/upload', {
                method: 'POST',
                body: formData
            });
            const data = await response.json();
            if (data.status === 'success') {
                appendConsoleLine(`SUCCESS: ${data.message}`, "rx");
                if (elements.csvFileInput) elements.csvFileInput.value = "";
                
                if (data.targets) {
                    updateTargetsListUI(data.targets);
                    if (data.targets.length > 0) {
                        const lastTarget = data.targets[data.targets.length - 1];
                        map.flyTo([lastTarget.latitude, lastTarget.longitude], 18, {
                            animate: true,
                            duration: 1.5
                        });
                        setTimeout(() => {
                            if (targetMarkers[lastTarget.timestamp]) {
                                targetMarkers[lastTarget.timestamp].openPopup();
                            }
                        }, 1600);
                    }
                }
            } else {
                appendConsoleLine(`FAIL: ${data.message}`, "err");
            }
        } catch (err) {
            appendConsoleLine(`FAIL: Network error uploading CSV: ${err.message}`, "err");
        }
    }

    // Helper: Append formatted console lines
    function appendConsoleLine(message, type = "log") {
        if (!elements.consoleLogBox) return;
        const line = document.createElement('div');
        line.className = `trace-${type}`;
        
        // Format timestamp in 12-hour hours:minutes (no seconds)
        const d = new Date();
        let hours = d.getHours();
        const minutes = d.getMinutes();
        const ampm = hours >= 12 ? 'PM' : 'AM';
        hours = hours % 12;
        hours = hours ? hours : 12;
        const hStr = hours < 10 ? '0' + hours : '' + hours;
        const mStr = minutes < 10 ? '0' + minutes : '' + minutes;
        const timeStr = `${hStr}:${mStr} ${ampm}`;
        line.textContent = `[${timeStr}] ${message}`;
        
        elements.consoleLogBox.appendChild(line);
        elements.consoleLogBox.scrollTop = elements.consoleLogBox.scrollHeight;
    }

    // Fetch active serial ports (removes simulated labels)
    async function fetchSerialPorts() {
        try {
            const response = await fetch('/api/serial/ports');
            const data = await response.json();
            if (data.ports && elements.serialPortSelect) {
                elements.serialPortSelect.innerHTML = `<option value="" disabled selected>Select Serial Port...</option>`;
                if (data.ports.length === 0) {
                    const opt = document.createElement('option');
                    opt.value = "";
                    opt.disabled = true;
                    opt.textContent = "No COM Ports Detected";
                    elements.serialPortSelect.appendChild(opt);
                } else {
                    data.ports.forEach(port => {
                        const opt = document.createElement('option');
                        opt.value = port;
                        opt.textContent = port;
                        elements.serialPortSelect.appendChild(opt);
                    });
                }
            }
        } catch (e) {
            // Silently swallow
        }
    }

    // --- DOM Event Listeners ---
    function initEventListeners() {
        // Serial Connect / Disconnect
        elements.connectSerialBtn.addEventListener('click', () => {
            toggleSerialConnection(true);
            setTimeout(() => {
                if (elements.settingsModal && elements.settingsModal.classList.contains('active')) {
                    elements.settingsModal.classList.remove('active');
                }
            }, 300);
        });
        elements.disconnectSerialBtn.addEventListener('click', () => {
            toggleSerialConnection(false);
        });

        // Settings Modal Toggle
        if (elements.openSettingsBtn) {
            elements.openSettingsBtn.addEventListener('click', () => {
                fetchSerialPorts();
                if (elements.settingsModal) elements.settingsModal.classList.add('active');
            });
        }
        if (elements.closeSettingsModalBtn) {
            elements.closeSettingsModalBtn.addEventListener('click', () => {
                if (elements.settingsModal) elements.settingsModal.classList.remove('active');
            });
        }
        if (elements.settingsModal) {
            elements.settingsModal.addEventListener('click', (e) => {
                if (e.target === elements.settingsModal) {
                    elements.settingsModal.classList.remove('active');
                }
            });
        }

        // Master Target Select Checkbox
        if (elements.markAllTargets) {
            elements.markAllTargets.addEventListener('change', function() {
                const checked = this.checked;
                const checkboxes = elements.targetsList.querySelectorAll('.target-checkbox');
                checkboxes.forEach(cb => {
                    cb.checked = checked;
                    const ts = cb.dataset.timestamp;
                    if (checked) {
                        markedTargetTimestamps.add(ts);
                    } else {
                        markedTargetTimestamps.delete(ts);
                    }
                });
                updateDeleteButtonState();
            });
        }

        // Delete Database items (Compact button)
        elements.deleteTargetBtn.addEventListener('click', deleteSelectedTarget);

        // Main Autonomous Button: opens modal to define boundary (if stopped) or stop (if active)
        if (elements.autoToggleBtn) {
            elements.autoToggleBtn.addEventListener('click', function() {
                openBoundaryModal();
            });
        }

        // Boundary Modal Close button
        if (elements.cancelBoundaryBtn) {
            elements.cancelBoundaryBtn.addEventListener('click', closeBoundaryModal);
        }

        // Boundary Modal Reset Points button
        if (elements.resetBoundaryBtn) {
            elements.resetBoundaryBtn.addEventListener('click', resetBoundaryData);
        }

        // Boundary Modal Start / Stop button
        if (elements.startBoundaryAutoBtn) {
            elements.startBoundaryAutoBtn.addEventListener('click', function() {
                if (isAutonomousActive) {
                    toggleAutonomousMode(false);
                } else {
                    submitBoundaryAndStartAutonomous();
                }
            });
        }

        // Fullscreen Mode Handler
        if (elements.fullScreenBtn) {
            elements.fullScreenBtn.addEventListener('click', function() {
                if (!document.fullscreenElement) {
                    elements.hmiRoot.requestFullscreen().catch(err => {
                        appendConsoleLine(`FAIL: Screen resize failed: ${err.message}`, "err");
                    });
                    this.innerHTML = `<i data-lucide="minimize"></i><span>EXIT FULLSCREEN</span>`;
                } else {
                    document.exitFullscreen();
                    this.innerHTML = `<i data-lucide="maximize"></i><span>FULL SCREEN</span>`;
                }
                if (window.lucide) lucide.createIcons();
            });
        }

        // Serial Data Stream Overlay Toggle (Hide/Show)
        if (elements.terminalToggleBtn && elements.mapTerminalOverlay) {
            elements.terminalToggleBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                const isCollapsed = elements.mapTerminalOverlay.classList.toggle('collapsed');
                if (elements.terminalToggleIcon) {
                    elements.terminalToggleIcon.setAttribute('data-lucide', isCollapsed ? 'chevron-down' : 'chevron-up');
                    if (window.lucide) lucide.createIcons();
                }
            });
        }

        // Download CSV action (Compact button)
        if (elements.downloadCsvBtn) {
            elements.downloadCsvBtn.addEventListener('click', function() {
                appendConsoleLine("SYSTEM: Downloading live detections CSV copy...", "log");
                window.location.href = '/api/targets/download';
            });
        }

        // Upload CSV trigger handlers (Compact button)
        if (elements.uploadCsvTriggerBtn && elements.csvFileInput) {
            elements.uploadCsvTriggerBtn.addEventListener('click', function() {
                elements.csvFileInput.click();
            });

            elements.csvFileInput.addEventListener('change', function(e) {
                const file = e.target.files[0];
                if (file) {
                    handleCSVUpload(file);
                }
            });
        }

        // Drag & Drop on Detection Log container
        if (elements.targetsListContainer) {
            const container = elements.targetsListContainer;
            
            ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
                container.addEventListener(eventName, (e) => {
                    e.preventDefault();
                    e.stopPropagation();
                }, false);
            });

            ['dragenter', 'dragover'].forEach(eventName => {
                container.addEventListener(eventName, () => {
                    container.classList.add('dragover');
                }, false);
            });

            ['dragleave', 'drop'].forEach(eventName => {
                container.addEventListener(eventName, () => {
                    container.classList.remove('dragover');
                }, false);
            });

            container.addEventListener('drop', (e) => {
                const dt = e.dataTransfer;
                const files = dt.files;
                if (files && files.length > 0) {
                    const file = files[0];
                    if (file.name.toLowerCase().endsWith('.csv')) {
                        handleCSVUpload(file);
                    } else {
                        appendConsoleLine("FAIL: Only CSV files are supported for detection log upload", "err");
                    }
                }
            }, false);
        }
    }

    // Run core engine boot
    initialize();
});

