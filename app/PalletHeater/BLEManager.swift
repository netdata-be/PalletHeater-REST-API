import Foundation
import CoreBluetooth
import SwiftUI

private enum UUID {
    static let stoveService = CBUUID(string: "A1B20001-1234-5678-ABCD-EF0123456789")
    static let stoveState   = CBUUID(string: "A1B20002-1234-5678-ABCD-EF0123456789")
    static let power        = CBUUID(string: "A1B20003-1234-5678-ABCD-EF0123456789")
    static let setpoint     = CBUUID(string: "A1B20004-1234-5678-ABCD-EF0123456789")
    static let fan          = CBUUID(string: "A1B20005-1234-5678-ABCD-EF0123456789")
    static let flame        = CBUUID(string: "A1B20006-1234-5678-ABCD-EF0123456789")
    static let roomTemp     = CBUUID(string: "A1B20007-1234-5678-ABCD-EF0123456789")
    static let fumeTemp     = CBUUID(string: "A1B20008-1234-5678-ABCD-EF0123456789")
    static let beep         = CBUUID(string: "A1B20009-1234-5678-ABCD-EF0123456789")
    static let lowPallet    = CBUUID(string: "A1B2000A-1234-5678-ABCD-EF0123456789")
    static let fumeFanRPM   = CBUUID(string: "A1B2000B-1234-5678-ABCD-EF0123456789")
    static let combustion   = CBUUID(string: "A1B2000C-1234-5678-ABCD-EF0123456789")
    static let pellet       = CBUUID(string: "A1B2000D-1234-5678-ABCD-EF0123456789")
    static let ipAddress    = CBUUID(string: "A1B2000E-1234-5678-ABCD-EF0123456789")
    static let info         = CBUUID(string: "A1B2000F-1234-5678-ABCD-EF0123456789")
    static let energySavingStart = CBUUID(string: "A1B20014-1234-5678-ABCD-EF0123456789")
    static let energySavingStop  = CBUUID(string: "A1B20015-1234-5678-ABCD-EF0123456789")
    static let wifi         = CBUUID(string: "A1B20013-1234-5678-ABCD-EF0123456789")
    static let restart      = CBUUID(string: "A1B20016-1234-5678-ABCD-EF0123456789")
    static let hostname     = CBUUID(string: "A1B20017-1234-5678-ABCD-EF0123456789")
    static let netConfig    = CBUUID(string: "A1B20018-1234-5678-ABCD-EF0123456789")
    static let timer1       = CBUUID(string: "A1B20020-1234-5678-ABCD-EF0123456789")
    static let timer2       = CBUUID(string: "A1B20021-1234-5678-ABCD-EF0123456789")
    static let timer3       = CBUUID(string: "A1B20022-1234-5678-ABCD-EF0123456789")
    static let timer4       = CBUUID(string: "A1B20023-1234-5678-ABCD-EF0123456789")
    static let timer5       = CBUUID(string: "A1B20024-1234-5678-ABCD-EF0123456789")
    static let timer6       = CBUUID(string: "A1B20025-1234-5678-ABCD-EF0123456789")
    static let timerUUIDs   = [timer1, timer2, timer3, timer4, timer5, timer6]
    static let stoveCharacteristics = [
        stoveState, power, setpoint, fan, flame, roomTemp, fumeTemp, beep,
        lowPallet, fumeFanRPM, combustion, pellet, ipAddress, info,
        energySavingStart, energySavingStop, hostname, netConfig,
        timer1, timer2, timer3, timer4, timer5, timer6
    ]
}

enum ConnectionMode: Equatable {
    case bluetooth
    case wifi(host: String)
    var isWiFi: Bool { if case .wifi = self { return true }; return false }
    var host: String? { if case .wifi(let h) = self { return h }; return nil }
}

enum BLEState {
    case idle, scanning, connecting, connected, disconnected, bluetoothOff
    var label: String {
        switch self {
        case .idle:         return "Tap to scan"
        case .scanning:     return "Scanning for PalletHeater…"
        case .connecting:   return "Connecting…"
        case .connected:    return "Connected"
        case .disconnected: return "Reconnecting…"
        case .bluetoothOff: return "Bluetooth is off"
        }
    }
}

struct NetworkConfig {
    enum Mode { case dhcp, staticIP }
    var mode: Mode = .dhcp
    var ip: String = ""
    var gateway: String = ""
    var subnet: String = "255.255.255.0"
    var dns: String = "8.8.8.8"

    init(raw: String) {
        let parts = raw.split(separator: ",", omittingEmptySubsequences: false).map(String.init)
        if parts.first == "static", parts.count >= 5 {
            mode = .staticIP
            ip = parts[1]; gateway = parts[2]; subnet = parts[3]; dns = parts[4]
        }
    }

    init() {}

    var encoded: String {
        mode == .dhcp ? "dhcp" : "static,\(ip),\(gateway),\(subnet),\(dns)"
    }

    var isValid: Bool {
        guard mode == .staticIP else { return true }
        return isValidIP(ip) && isValidIP(gateway) && isValidIP(subnet)
    }

    private func isValidIP(_ s: String) -> Bool {
        let parts = s.split(separator: ".").compactMap { Int($0) }
        return parts.count == 4 && parts.allSatisfy { $0 >= 0 && $0 <= 255 }
    }
}

class BLEManager: NSObject, ObservableObject {
    // Connection
    @Published var connectionMode: ConnectionMode = .bluetooth
    @Published var bleState: BLEState = .idle
    @Published var isConnected = false
    @Published var hasConnected = false
    @Published var lastDisconnectError: String = ""
    @Published var rssi: Int?

    var signalQualityLabel: String {
        guard let rssi else { return "Unknown" }
        if rssi >= -67 { return "Good" }
        if rssi >= -80 { return "Okay" }
        return "Bad"
    }

    var signalDescription: String {
        guard let rssi else { return "Signal unknown" }
        return "\(signalQualityLabel) (\(rssi) dBm)"
    }

    var signalIconName: String {
        guard let rssi else { return "antenna.radiowaves.left.and.right.slash" }
        if rssi >= -67 { return "wifi" }
        if rssi >= -80 { return "wifi.exclamationmark" }
        return "wifi.slash"
    }

    var wifiSignalLabel: String {
        guard let rssi = wifiRSSI, rssi < 0 else { return "Not connected" }
        if rssi >= -50 { return "Excellent" }
        if rssi >= -60 { return "Good" }
        if rssi >= -70 { return "Fair" }
        return "Weak"
    }

    var wifiSignalColor: Color {
        guard let rssi = wifiRSSI, rssi < 0 else { return .secondary }
        if rssi >= -50 { return .green }
        if rssi >= -60 { return .green }
        if rssi >= -70 { return .orange }
        return .red
    }

    var wifiIconName: String {
        guard let rssi = wifiRSSI, rssi < 0 else { return "wifi.slash" }
        if rssi >= -60 { return "wifi" }
        if rssi >= -70 { return "wifi.exclamationmark" }
        return "wifi.slash"
    }

    // Stove readings
    @Published var stoveStatus: StoveStatus = .unknown
    @Published var isPowered = false
    @Published var setpointTemp: Int = 20
    @Published var fanSpeed: Int = 1
    @Published var flameLevel: Int = 2
    @Published var roomTemp: Double = 0
    @Published var fumeTemp: Int = 0
    @Published var beepEnabled = true
    @Published var lowPallet = false
    @Published var fumeFanRPM: Int = 0
    @Published var combustionMode: CombustionMode = .adjusted(0)
    @Published var pelletAdjValue: Int = 0
    @Published var ipAddress: String = ""
    @Published var wifiSSID: String = ""
    @Published var wifiRSSI: Int? = nil
    @Published var firmwareVersion: String = ""
    @Published var totalRuntime: Int = 0
    @Published var numberOfStarts: Int = 0
    @Published var previousErrors: [StovePreviousError] = []
    @Published var remoteControlTemp: Double? = nil
    @Published var hoursSinceLastService: Int? = nil
    @Published var stoveTime: Date? = nil
    @Published var pendingCommandCount = 0
    @Published var commandStatus: String = ""
    @Published var deviceHostname: String = ""
    @Published var networkConfig = NetworkConfig()
    @Published var isRebooting = false
    @Published var rebootStatus: String = ""
    @Published var pendingInitialReadCount = 0
    @Published var initialReadTotalCount = 0
    @Published var energySavingEnabled = false
    @Published var energySavingStartOffset = 0
    @Published var energySavingStopOffset = 0
    @Published var energySavingStatus: String = ""
    @Published var timers: [StoveTimer] = Array(
        repeating: StoveTimer(enabled: false, on: nil, off: nil, enabledOn: nil, settings: nil),
        count: 6
    )

    private var pendingInitialReadUUIDs: Set<CBUUID> = []
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var chars: [CBUUID: CBCharacteristic] = [:]
    private var scanTimeoutTimer: Timer?
    private var reconnectTimer: Timer?
    private var rssiTimer: Timer?
    private var wifiPollTask: Task<Void, Never>?
    private var shouldAutoReconnect = false
    private var connectedAt: Date?
    private var serviceDiscoveryRetries = 0
    private var characteristicDiscoveryRetries = 0
    private var expectedValues: [CBUUID: Data] = [:]
    private var commandTimeouts: [CBUUID: Timer] = [:]

    var hasPendingCommand: Bool {
        pendingCommandCount > 0
    }

    private func dashboardBool(_ key: String) -> Bool {
        if let value = UserDefaults.standard.object(forKey: key) as? Bool {
            return value
        }
        return true
    }

    private var shouldReadInfoChar: Bool {
        dashboardBool(DashboardConfig.showRemoteTemp) ||
        dashboardBool(DashboardConfig.showServiceHours) ||
        dashboardBool(DashboardConfig.showStoveTime) ||
        dashboardBool(DashboardConfig.showSystemInfo) ||
        dashboardBool(DashboardConfig.showPreviousErrors)
    }

    func refreshDashboardInfoIfNeeded() {
        guard shouldReadInfoChar,
              let p = peripheral,
              let c = chars[UUID.info],
              isConnected,
              p.state == .connected else {
            return
        }
        p.setNotifyValue(true, for: c)
        p.readValue(for: c)
    }

    func isCommandPending(_ uuid: CBUUID) -> Bool {
        expectedValues[uuid] != nil
    }

    func isTimerPending(_ index: Int) -> Bool {
        guard index >= 0, index < UUID.timerUUIDs.count else { return false }
        return isCommandPending(UUID.timerUUIDs[index])
    }

    var isPopulating: Bool { pendingInitialReadCount > 0 }
    var initialReadProgressText: String {
        guard initialReadTotalCount > 0 else { return "" }
        let done = initialReadTotalCount - pendingInitialReadCount
        return "Loading values — \(done)/\(initialReadTotalCount)"
    }

    var energySavingStartTemperature: Int {
        setpointTemp + energySavingStartOffset
    }

    var energySavingStopTemperature: Int {
        setpointTemp + energySavingStopOffset
    }

    var stoveTimeText: String {
        guard let stoveTime else { return "—" }
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: stoveTime)
    }

    private func updateEnergySavingEnabled() {
        energySavingEnabled = energySavingStartOffset != 0 || energySavingStopOffset != 0
        energySavingStatus = energySavingEnabled ? "Energy saving active" : "Energy saving disabled"
    }

    var isEnergySavingPending: Bool {
        isCommandPending(UUID.energySavingStart) || isCommandPending(UUID.energySavingStop)
    }

    var isPowerPending: Bool { isCommandPending(UUID.power) }
    var isSetpointPending: Bool { isCommandPending(UUID.setpoint) }
    var isFanPending: Bool { isCommandPending(UUID.fan) }
    var isFlamePending: Bool { isCommandPending(UUID.flame) }
    var isBeepPending: Bool { isCommandPending(UUID.beep) }
    var isCombustionPending: Bool { isCommandPending(UUID.combustion) }
    var isPelletPending: Bool { isCommandPending(UUID.pellet) }

    override init() {
        super.init()
        UserDefaults.standard.register(defaults: [
            DashboardConfig.showRemoteTemp: true,
            DashboardConfig.showPelletQuality: true,
            DashboardConfig.showEnergySaving: true,
            DashboardConfig.showServiceHours: true,
            DashboardConfig.showStoveTime: true,
            DashboardConfig.showSystemInfo: true,
            DashboardConfig.showPreviousErrors: true
        ])
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: - Public API

    func startScan() {
        shouldAutoReconnect = true
        reconnectTimer?.invalidate()
        guard central.state == .poweredOn else { bleState = .bluetoothOff; return }

        if bleState == .connected || bleState == .connecting { return }
        if central.isScanning { central.stopScan() }

        if let p = peripheral, p.state == .connected {
            isConnected = true
            hasConnected = true
            bleState = .connected
            return
        }

        bleState = .scanning
        scanTimeoutTimer?.invalidate()
        central.scanForPeripherals(withServices: [UUID.stoveService],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        scanTimeoutTimer = Timer.scheduledTimer(withTimeInterval: 8.0, repeats: false) { [weak self] _ in
            guard let self, self.bleState == .scanning else { return }
            self.central.stopScan()
            self.scheduleReconnect(after: 1.0)
        }
    }

    // User-initiated disconnect — clears everything and goes back to ScanView.
    func disconnect() {
        shouldAutoReconnect = false
        scanTimeoutTimer?.invalidate()
        reconnectTimer?.invalidate()
        stopRSSIUpdates()
        central.stopScan()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil
        chars = [:]
        rssi = nil
        connectedAt = nil
        clearPendingCommands(status: "")
        clearInitialReadProgress()
        clearEnergySavingState()
        isConnected = false
        hasConnected = false
        bleState = .idle
    }

    // MARK: - Write commands

    func setPower(_ on: Bool) {
        if let h = connectionMode.host { isPowered = on; wifiPost(h, "/api/power", "state=\(on ? "on" : "off")") }
        else { write(UInt8(on ? 1 : 0), to: UUID.power) }
    }
    func setTemperature(_ temp: Int) {
        let v = min(30, max(7, temp))
        if let h = connectionMode.host { setpointTemp = v; wifiPost(h, "/api/setpoint", "value=\(v)") }
        else { write(UInt8(clamping: v), to: UUID.setpoint) }
    }
    func setFan(_ speed: Int) {
        if let h = connectionMode.host { fanSpeed = speed; wifiPost(h, "/api/fan", "speed=\(speed)") }
        else { write(UInt8(clamping: speed), to: UUID.fan) }
    }
    func setFlame(_ level: Int) {
        if let h = connectionMode.host { flameLevel = level; wifiPost(h, "/api/flame", "power=\(level)") }
        else { write(UInt8(clamping: level), to: UUID.flame) }
    }
    func setBeep(_ enabled: Bool) {
        if let h = connectionMode.host { beepEnabled = enabled; wifiPost(h, "/api/beep", "enabled=\(enabled)") }
        else { write(UInt8(enabled ? 1 : 0), to: UUID.beep) }
    }
    func setNetConfig(_ config: NetworkConfig) {
        guard !connectionMode.isWiFi else { return }
        guard let p = peripheral, let c = chars[UUID.netConfig], isConnected else { return }
        guard let data = config.encoded.data(using: .utf8) else { return }
        markPending(UUID.netConfig, expected: data, label: "Applying network config")
        p.writeValue(data, for: c, type: .withResponse)
    }

    func setHostname(_ name: String) {
        guard !connectionMode.isWiFi else { return }
        guard let p = peripheral, let c = chars[UUID.hostname], isConnected else { return }
        guard let data = name.data(using: .utf8) else { return }
        markPending(UUID.hostname, expected: data, label: "Applying hostname")
        p.writeValue(data, for: c, type: .withResponse)
    }
    func setCombustion(_ mode: CombustionMode) {
        guard !connectionMode.isWiFi else { return }
        write(mode.raw, to: UUID.combustion)
    }
    func setPellet(_ adj: Int) {
        guard !connectionMode.isWiFi else { return }
        write(pelletRaw(from: adj), to: UUID.pellet)
    }
    func setEnergySavingEnabled(_ enabled: Bool) {
        energySavingStatus = enabled ? "Enabling energy saving" : "Disabling energy saving"
        if let h = connectionMode.host {
            energySavingEnabled = enabled
            let start = enabled ? energySavingStartOffset : 0
            let stop  = enabled ? energySavingStopOffset  : 0
            wifiPost(h, "/api/energysaving", "start=\(start)&stop=\(stop)")
        } else {
            if enabled {
                write(UInt8(bitPattern: Int8(energySavingStartOffset)), to: UUID.energySavingStart)
                write(UInt8(energySavingStopOffset), to: UUID.energySavingStop)
            } else {
                write(UInt8(bitPattern: Int8(0)), to: UUID.energySavingStart)
                write(UInt8(0), to: UUID.energySavingStop)
            }
        }
    }
    func setEnergySaving(startTemperature: Int, stopTemperature: Int) {
        let clampedStart = max(-3, min(0, startTemperature - setpointTemp))
        let clampedStop  = max(0,  min(3, stopTemperature  - setpointTemp))
        energySavingStartOffset = clampedStart
        energySavingStopOffset  = clampedStop
        energySavingStatus = "Updating energy saving"
        if let h = connectionMode.host {
            wifiPost(h, "/api/energysaving", "start=\(clampedStart)&stop=\(clampedStop)")
        } else {
            write(UInt8(bitPattern: Int8(clampedStart)), to: UUID.energySavingStart)
            write(UInt8(clampedStop), to: UUID.energySavingStop)
        }
    }

    func removePreviousError(id: Int) {
        previousErrors.removeAll { $0.id == id }
    }

    func forceRefresh() async {
        if let h = connectionMode.host {
            await wifiPollState(host: h)
            await wifiPollTimers(host: h)
            await wifiPollEnergySaving(host: h)
        } else {
            guard isConnected, let p = peripheral, p.state == .connected else { return }
            for char in chars.values where char.properties.contains(.read) {
                p.readValue(for: char)
            }
            try? await Task.sleep(nanoseconds: 2_000_000_000)
        }
    }

    func reboot() {
        if let h = connectionMode.host {
            isRebooting = true
            rebootStatus = "Rebooting…"
            Task {
                _ = try? await URLSession.shared.data(from: URL(string: "http://\(h)/restart")!)
                await MainActor.run { self.isRebooting = false; self.rebootStatus = "Restart sent" }
            }
        } else {
            guard let p = peripheral, let c = chars[UUID.restart], isConnected else {
                rebootStatus = "Not connected"; return
            }
            isRebooting = true
            rebootStatus = "Rebooting…"
            p.writeValue(Data([0x01]), for: c, type: .withoutResponse)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                self?.isRebooting = false; self?.rebootStatus = "Restart sent"
            }
        }
    }

    func setTimer(_ timer: StoveTimer, at index: Int) {
        guard index >= 0, index < 6 else { return }
        timers[index] = timer
        if let h = connectionMode.host {
            wifiPostJSON(h, "/api/timers/pr\(index + 1)", body: timer)
        } else {
            let uuid = UUID.timerUUIDs[index]
            guard let p = peripheral, let c = chars[uuid] else { return }
            let data = timer.bleBytes
            markPending(uuid, expected: data, label: "Applying PR\(index + 1)")
            p.writeValue(data, for: c, type: .withResponse)
        }
    }

    private func write(_ byte: UInt8, to uuid: CBUUID) {
        guard let p = peripheral, let c = chars[uuid] else { return }
        let data = Data([byte])
        applyOptimisticValue(uuid: uuid, data: data)
        markPending(uuid, expected: data, label: "Applying \(commandName(for: uuid))")
        p.writeValue(data, for: c, type: .withResponse)
    }

    // MARK: - Parse incoming data

    private func handle(uuid: CBUUID, data: Data) {
        guard !data.isEmpty else { return }
        switch uuid {
        case UUID.stoveState:  stoveStatus    = StoveStatus(raw: data[0])
        case UUID.power:       isPowered      = data[0] == 1
        case UUID.setpoint:    setpointTemp   = Int(data[0])
        case UUID.fan:         fanSpeed       = Int(data[0])
        case UUID.flame:       flameLevel     = Int(data[0])
        case UUID.roomTemp:
            guard data.count >= 2 else { return }
            roomTemp = Double(Int16(bitPattern: UInt16(data[0]) | UInt16(data[1]) << 8)) / 10.0
        case UUID.fumeTemp:    fumeTemp       = Int(data[0])
        case UUID.beep:        beepEnabled    = data[0] == 1
        case UUID.lowPallet:   lowPallet      = data[0] == 1
        case UUID.fumeFanRPM:
            guard data.count >= 2 else { return }
            fumeFanRPM = Int(UInt16(data[0]) | UInt16(data[1]) << 8)
        case UUID.combustion:  combustionMode = CombustionMode(raw: data[0])
        case UUID.pellet:      pelletAdjValue = pelletAdj(from: data[0])
        case UUID.energySavingStart:
            energySavingStartOffset = Int(Int8(bitPattern: data[0]))
            updateEnergySavingEnabled()
        case UUID.energySavingStop:
            energySavingStopOffset = Int(data[0])
            updateEnergySavingEnabled()
        case UUID.ipAddress:
            let ipData = data.filter { $0 != 0 }
            ipAddress = (String(data: ipData, encoding: .utf8) ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        case UUID.hostname:
            deviceHostname = (String(data: data.prefix { $0 != 0 }, encoding: .utf8) ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        case UUID.netConfig:
            let raw = (String(data: data, encoding: .utf8) ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            networkConfig = NetworkConfig(raw: raw)
        case UUID.wifi:
            handleWifi(data)
        case UUID.info:
            handleInfo(data)
        default:
            if let idx = UUID.timerUUIDs.firstIndex(of: uuid) {
                timers[idx] = StoveTimer(bleData: data)
            }
        }
        completePendingIfMatched(uuid: uuid, data: data)
    }

    private func handleInfo(_ data: Data) {
        guard data.count >= 20 else { return }
        let fwBytes = data.prefix(11).prefix { $0 != 0 }
        firmwareVersion = String(data: Data(fwBytes), encoding: .utf8) ?? ""
        totalRuntime = Int(UInt16(data[11]) | UInt16(data[12]) << 8)
        numberOfStarts = Int(UInt16(data[13]) | UInt16(data[14]) << 8)
        previousErrors = data[15..<20].enumerated()
            .filter { $0.element != 0 }
            .map { StovePreviousError(slot: $0.offset + 1, code: Int($0.element)) }

        if data.count >= 22 {
            hoursSinceLastService = Int(UInt16(data[20]) | UInt16(data[21]) << 8)
        } else {
            hoursSinceLastService = nil
        }

        if data.count >= 24 {
            let raw = Int16(bitPattern: UInt16(data[22]) | UInt16(data[23]) << 8)
            remoteControlTemp = raw == Int16.min ? nil : Double(raw) / 2.0
        } else {
            remoteControlTemp = nil
        }

        if data.count >= 28 {
            let timestamp = UInt32(data[24]) |
                UInt32(data[25]) << 8 |
                UInt32(data[26]) << 16 |
                UInt32(data[27]) << 24
            stoveTime = timestamp > 0 ? Date(timeIntervalSince1970: TimeInterval(timestamp)) : nil
        } else {
            stoveTime = nil
        }
    }

    private func handleWifi(_ data: Data) {
        guard data.count >= 1 else { return }
        let rssiRaw = Int(Int8(bitPattern: data[0]))
        wifiRSSI = rssiRaw < 0 ? rssiRaw : nil

        guard data.count > 1 else { wifiSSID = ""; return }
        let payload = data[1...]

        if let nullIdx = payload.firstIndex(of: 0) {
            // New format: [IP\0][SSID]
            let ipBytes = payload[payload.startIndex..<nullIdx]
            let parsedIP = (String(data: Data(ipBytes), encoding: .utf8) ?? "")
                .trimmingCharacters(in: .whitespacesAndNewlines)
            if !parsedIP.isEmpty { ipAddress = parsedIP }

            let afterNull = payload[payload.index(after: nullIdx)...]
            let ssidBytes = afterNull.prefix { $0 != 0 }
            wifiSSID = (String(data: Data(ssidBytes), encoding: .utf8) ?? "")
                .trimmingCharacters(in: .controlCharacters)
        } else {
            // Old format: [SSID only, no null]
            let ssidBytes = payload.prefix { $0 != 0 }
            wifiSSID = (String(data: Data(ssidBytes), encoding: .utf8) ?? "")
                .trimmingCharacters(in: .controlCharacters)
        }
    }

    private func applyOptimisticValue(uuid: CBUUID, data: Data) {
        guard let byte = data.first else { return }
        switch uuid {
        case UUID.power:      isPowered = byte == 1
        case UUID.setpoint:   setpointTemp = Int(byte)
        case UUID.fan:        fanSpeed = Int(byte)
        case UUID.flame:      flameLevel = Int(byte)
        case UUID.beep:       beepEnabled = byte == 1
        case UUID.combustion: combustionMode = CombustionMode(raw: byte)
        case UUID.pellet:     pelletAdjValue = pelletAdj(from: byte)
        case UUID.energySavingStart: energySavingStartOffset = Int(Int8(bitPattern: byte)); updateEnergySavingEnabled()
        case UUID.energySavingStop:  energySavingStopOffset = Int(byte); updateEnergySavingEnabled()
        default:              break
        }
    }

    private func markPending(_ uuid: CBUUID, expected data: Data, label: String) {
        expectedValues[uuid] = data
        commandTimeouts[uuid]?.invalidate()
        commandStatus = label
        updatePendingCount()
        commandTimeouts[uuid] = Timer.scheduledTimer(withTimeInterval: 12.0, repeats: false) { [weak self] _ in
            guard let self, self.expectedValues[uuid] != nil else { return }
            self.expectedValues.removeValue(forKey: uuid)
            self.commandTimeouts[uuid]?.invalidate()
            self.commandTimeouts.removeValue(forKey: uuid)
            self.commandStatus = "No confirmation from stove"
            self.updatePendingCount()
        }
    }

    private func completePendingIfMatched(uuid: CBUUID, data: Data) {
        guard let expected = expectedValues[uuid], expected == data else { return }
        expectedValues.removeValue(forKey: uuid)
        commandTimeouts[uuid]?.invalidate()
        commandTimeouts.removeValue(forKey: uuid)
        commandStatus = expectedValues.isEmpty ? "" : "Applying changes"
        updatePendingCount()
    }

    private func completeInitialReadIfNeeded(uuid: CBUUID) {
        guard pendingInitialReadUUIDs.remove(uuid) != nil else { return }
        pendingInitialReadCount = pendingInitialReadUUIDs.count
    }

    private func clearPendingCommands(status: String) {
        for timer in commandTimeouts.values { timer.invalidate() }
        commandTimeouts = [:]
        expectedValues = [:]
        pendingCommandCount = 0
        commandStatus = status
    }

    private func clearInitialReadProgress() {
        pendingInitialReadUUIDs.removeAll()
        pendingInitialReadCount = 0
        initialReadTotalCount = 0
    }

    private func clearEnergySavingState() {
        energySavingStatus = ""
        energySavingEnabled = false
        energySavingStartOffset = 0
        energySavingStopOffset = 0
    }

    private func updatePendingCount() {
        pendingCommandCount = expectedValues.count
    }

    private func commandName(for uuid: CBUUID) -> String {
        switch uuid {
        case UUID.power:      return "power"
        case UUID.setpoint:   return "setpoint"
        case UUID.fan:        return "fan speed"
        case UUID.flame:      return "flame power"
        case UUID.beep:       return "beep setting"
        case UUID.combustion: return "combustion"
        case UUID.pellet:     return "pellet quality"
        case UUID.energySavingStart: return "energy-saving start"
        case UUID.energySavingStop:  return "energy-saving stop"
        default:              return "change"
        }
    }

    private func scheduleReconnect(after delay: TimeInterval) {
        guard shouldAutoReconnect, !connectionMode.isWiFi else { return }
        reconnectTimer?.invalidate()
        bleState = .disconnected
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            self?.startScan()
        }
    }

    private func failCurrentConnection(_ message: String) {
        lastDisconnectError = message
        if let p = peripheral {
            central.cancelPeripheralConnection(p)
        } else {
            scheduleReconnect(after: 1.0)
        }
    }

    private func retryServiceDiscovery(_ message: String) {
        lastDisconnectError = message
        guard serviceDiscoveryRetries < 4,
              let p = peripheral,
              p.state == .connected else {
            failCurrentConnection(message)
            return
        }
        serviceDiscoveryRetries += 1
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { [weak self, weak p] in
            guard let self, let p, self.peripheral === p, p.state == .connected else { return }
            p.discoverServices([UUID.stoveService])
        }
    }

    private func retryCharacteristicDiscovery(for service: CBService, message: String) {
        lastDisconnectError = message
        guard characteristicDiscoveryRetries < 4,
              let p = peripheral,
              p.state == .connected else {
            failCurrentConnection(message)
            return
        }
        characteristicDiscoveryRetries += 1
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { [weak self, weak p, weak service] in
            guard let self, let p, let service, self.peripheral === p, p.state == .connected else { return }
            p.discoverCharacteristics(nil, for: service)
        }
    }

    private func startRSSIUpdates() {
        rssiTimer?.invalidate()
        rssiTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            guard let self, self.isConnected, self.peripheral?.state == .connected else { return }
            self.peripheral?.readRSSI()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) { [weak self] in
            guard let self, self.isConnected, self.peripheral?.state == .connected else { return }
            self.peripheral?.readRSSI()
        }
    }

    private func stopRSSIUpdates() {
        rssiTimer?.invalidate()
        rssiTimer = nil
    }

    private func startCharacteristicSetup(_ characteristics: [CBCharacteristic]) {
        chars = [:]
        let ordered = characteristics.sorted { $0.uuid.uuidString < $1.uuid.uuidString }
        for char in ordered {
            chars[char.uuid] = char
        }

        isConnected = true
        hasConnected = true
        bleState = .connected
        connectedAt = Date()
        startRSSIUpdates()

        let notifyChars = ordered.filter { $0.properties.contains(.notify) && (shouldReadInfoChar || $0.uuid != UUID.info) }
        let readChars = ordered.filter { $0.properties.contains(.read) && (shouldReadInfoChar || $0.uuid != UUID.info) }
        let earlyReadUUIDs: Set<CBUUID> = [
            UUID.ipAddress,
            UUID.wifi,
            UUID.hostname,
            UUID.netConfig,
            UUID.energySavingStart,
            UUID.energySavingStop
        ]
        let orderedReadChars = readChars.sorted { lhs, rhs in
            let lhsEarly = earlyReadUUIDs.contains(lhs.uuid)
            let rhsEarly = earlyReadUUIDs.contains(rhs.uuid)
            if lhsEarly != rhsEarly { return lhsEarly }
            return lhs.uuid.uuidString < rhs.uuid.uuidString
        }
        pendingInitialReadUUIDs = Set(orderedReadChars.map { $0.uuid })
        pendingInitialReadCount = pendingInitialReadUUIDs.count
        initialReadTotalCount = pendingInitialReadCount

        bleLog("connected — \(characteristics.count) chars, \(notifyChars.count) notifiable, \(pendingInitialReadCount) initial reads")

        // Subscribe and read in one burst after a single short connection-settling delay.
        // The firmware pushes the current value via notification on each subscribe, so
        // explicit reads are just a fallback — CoreBluetooth queues them all internally.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.10) { [weak self] in
            guard let self, self.isConnected, self.peripheral?.state == .connected else { return }
            for char in notifyChars {
                self.peripheral?.setNotifyValue(true, for: char)
            }
            for char in orderedReadChars {
                self.peripheral?.readValue(for: char)
            }
            self.bleLog("→ burst: \(notifyChars.count) subscribes + \(orderedReadChars.count) reads")
        }
    }

    private func bleLog(_ message: String) {
        let elapsed = connectedAt.map { -$0.timeIntervalSinceNow } ?? 0
        print(String(format: "[BLE +%5.2fs] %@", elapsed, message))
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            // Only resume if user already initiated a BLE scan session.
            if shouldAutoReconnect && !connectionMode.isWiFi &&
               (bleState == .idle || bleState == .bluetoothOff || bleState == .disconnected) {
                startScan()
            }
        case .poweredOff, .unauthorized, .unsupported:
            bleState = .bluetoothOff
            isConnected = false
            stopRSSIUpdates()
        default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        scanTimeoutTimer?.invalidate()
        self.peripheral = peripheral
        rssi = RSSI.intValue
        peripheral.delegate = self
        central.stopScan()
        bleState = .connecting
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        scanTimeoutTimer?.invalidate()
        reconnectTimer?.invalidate()
        isConnected = false
        bleState = .connecting
        chars = [:]
        connectedAt = nil
        serviceDiscoveryRetries = 0
        characteristicDiscoveryRetries = 0
        peripheral.delegate = self
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.30) { [weak self, weak peripheral] in
            guard let self, let peripheral, self.peripheral === peripheral, peripheral.state == .connected else { return }
            peripheral.discoverServices([UUID.stoveService])
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        isConnected = false
        bleState = .disconnected
        stopRSSIUpdates()
        scheduleReconnect(after: 1.0)
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        isConnected = false
        bleState = .disconnected
        chars = [:]
        stopRSSIUpdates()
        clearPendingCommands(status: "")
        clearInitialReadProgress()
        clearEnergySavingState()
        let uptime = connectedAt.map { Date().timeIntervalSince($0) } ?? 0
        connectedAt = nil
        if let e = error {
            lastDisconnectError = "code \((e as NSError).code): \(e.localizedDescription)"
        } else {
            lastDisconnectError = "clean disconnect"
        }
        scheduleReconnect(after: uptime < 1.0 ? 2.5 : 0.8)
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            retryServiceDiscovery("service discovery: \(error.localizedDescription)")
            return
        }
        guard let services = peripheral.services else {
            retryServiceDiscovery("service discovery returned no services")
            return
        }
        for service in services where service.uuid == UUID.stoveService {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self, weak peripheral, weak service] in
                guard let self, let peripheral, let service, self.peripheral === peripheral, peripheral.state == .connected else { return }
                peripheral.discoverCharacteristics(nil, for: service)
            }
            return
        }
        retryServiceDiscovery("stove service missing")
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error {
            retryCharacteristicDiscovery(for: service, message: "characteristics: \(error.localizedDescription)")
            return
        }
        guard let characteristics = service.characteristics else {
            retryCharacteristicDiscovery(for: service, message: "characteristic discovery returned no characteristics")
            return
        }
        guard !characteristics.isEmpty else {
            retryCharacteristicDiscovery(for: service, message: "characteristic discovery returned empty list")
            return
        }
        startCharacteristicSetup(characteristics)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            lastDisconnectError = "read \(characteristic.uuid): \(error.localizedDescription)"
            return
        }
        guard let data = characteristic.value else { return }
        bleLog("← \(characteristic.uuid.uuidString.prefix(8))… \(data.count)B  \(data.prefix(4).map { String(format:"%02X",$0) }.joined(separator:" "))")
        handle(uuid: characteristic.uuid, data: data)
        completeInitialReadIfNeeded(uuid: characteristic.uuid)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            lastDisconnectError = "notify \(characteristic.uuid): \(error.localizedDescription)"
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            print("[BLE] write error \(characteristic.uuid): \(error)")
            expectedValues.removeValue(forKey: characteristic.uuid)
            commandTimeouts[characteristic.uuid]?.invalidate()
            commandTimeouts.removeValue(forKey: characteristic.uuid)
            commandStatus = "Write failed"
            updatePendingCount()
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didReadRSSI RSSI: NSNumber,
                    error: Error?) {
        guard error == nil else { return }
        rssi = RSSI.intValue
    }

    // MARK: - WiFi mode

    func connectWiFi(host: String) {
        let h = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !h.isEmpty else { return }
        UserDefaults.standard.set(h, forKey: "lastWiFiHost")
        connectionMode = .wifi(host: h)
        ipAddress = h
        hasConnected = true
        isConnected = false
        pendingInitialReadCount = 3
        initialReadTotalCount = 3
        wifiPollTask?.cancel()
        wifiPollTask = Task { [weak self] in await self?.wifiRun(host: h) }
    }

    func disconnectWiFi() {
        wifiPollTask?.cancel()
        wifiPollTask = nil
        connectionMode = .bluetooth
        hasConnected = false
        isConnected = false
        pendingInitialReadCount = 0
        initialReadTotalCount = 0
        ipAddress = ""; wifiSSID = ""; deviceHostname = ""
        stoveStatus = .unknown; isPowered = false
        timers = Array(repeating: StoveTimer(enabled: false, on: nil, off: nil, enabledOn: nil, settings: nil), count: 6)
    }

    func switchToWiFi() {
        let host = ipAddress
        guard !host.isEmpty else { return }
        // Cancel BLE without resetting hasConnected so the main view doesn't flash
        shouldAutoReconnect = false
        scanTimeoutTimer?.invalidate()
        reconnectTimer?.invalidate()
        stopRSSIUpdates()
        central.stopScan()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil
        chars = [:]
        rssi = nil
        connectedAt = nil
        clearPendingCommands(status: "")
        clearInitialReadProgress()
        isConnected = false
        bleState = .idle
        connectWiFi(host: host)
    }

    func switchToBLE() {
        disconnectWiFi()
        startScan()
    }

    private func wifiRun(host: String) async {
        async let s: () = wifiPollState(host: host)
        async let t: () = wifiPollTimers(host: host)
        async let e: () = wifiPollEnergySaving(host: host)
        _ = await (s, t, e)
        await MainActor.run {
            self.isConnected = true
            self.pendingInitialReadCount = 0
        }
        var tick = 0
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 2_000_000_000)
            guard !Task.isCancelled, case .wifi(let h) = connectionMode else { break }
            await wifiPollState(host: h)
            tick += 1
            if tick % 15 == 0 {
                await wifiPollTimers(host: h)
                await wifiPollEnergySaving(host: h)
            }
        }
    }

    private func wifiPollState(host: String) async {
        guard let url = URL(string: "http://\(host)/api/state") else { return }
        do {
            let (data, _) = try await URLSession.shared.data(from: url)
            guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }
            await MainActor.run { self.applyWiFiStateJSON(json) }
        } catch {
            await MainActor.run { self.isConnected = false }
        }
    }

    private func wifiPollTimers(host: String) async {
        guard let url = URL(string: "http://\(host)/api/timers") else { return }
        guard let (data, _) = try? await URLSession.shared.data(from: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let timersJSON = json["timers"] as? [String: Any] else { return }
        let prNames = ["PR1","PR2","PR3","PR4","PR5","PR6"]
        var updated = timers
        for (i, name) in prNames.enumerated() {
            if let obj = timersJSON[name],
               let timerData = try? JSONSerialization.data(withJSONObject: obj),
               let t = try? JSONDecoder().decode(StoveTimer.self, from: timerData) {
                updated[i] = t
            }
        }
        await MainActor.run { self.timers = updated }
    }

    private func wifiPollEnergySaving(host: String) async {
        guard let url = URL(string: "http://\(host)/api/energysaving") else { return }
        guard let (data, _) = try? await URLSession.shared.data(from: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let es = json["energySaving"] as? [String: Any] else { return }
        await MainActor.run {
            self.energySavingEnabled      = es["enabled"] as? Bool ?? false
            self.energySavingStartOffset  = es["start"]   as? Int  ?? 0
            self.energySavingStopOffset   = es["stop"]    as? Int  ?? 0
            self.updateEnergySavingEnabled()
        }
    }

    @MainActor
    private func applyWiFiStateJSON(_ json: [String: Any]) {
        isConnected = true
        if let ss = json["stoveState"] as? [String: Any] {
            stoveStatus = StoveStatus(raw: UInt8(clamping: ss["stoveState"] as? Int ?? 0))
            isPowered   = ss["poweredOn"] as? Bool ?? isPowered
        }
        if let flame = json["flame"] as? [String: Any] {
            flameLevel = flame["power"] as? Int ?? flameLevel
        }
        if let temps = json["temperatures"] as? [String: Any] {
            roomTemp     = temps["room"]     as? Double ?? roomTemp
            fumeTemp     = temps["fume"]     as? Int    ?? fumeTemp
            setpointTemp = temps["setPoint"] as? Int    ?? setpointTemp
            let rc = temps["remoteControl"] as? Double ?? 0
            remoteControlTemp = rc > 0 ? rc : nil
        }
        fanSpeed        = json["RoomFanSpeed"]      as? Int  ?? fanSpeed
        lowPallet       = (json["lowPalletAlarm"]   as? Int  ?? 0) != 0
        beepEnabled     = json["stoveBeepEnabled"]  as? Bool ?? beepEnabled
        numberOfStarts  = json["numberOfStarts"]    as? Int  ?? numberOfStarts
        fumeFanRPM      = json["fumeFanRPM"]        as? Int  ?? fumeFanRPM
        if let errors = json["previousErrors"] as? [String: Any] {
            previousErrors = (1...5).compactMap { slot -> StovePreviousError? in
                guard let e = errors["MemoryPos_\(slot)"] as? [String: Any],
                      let id = e["id"] as? Int, id != 0 else { return nil }
                return StovePreviousError(slot: slot, code: id)
            }
        }
    }

    private func wifiPost(_ host: String, _ path: String, _ query: String) {
        Task {
            guard let url = URL(string: "http://\(host)\(path)?\(query)") else { return }
            var req = URLRequest(url: url)
            req.httpMethod = "POST"
            req.timeoutInterval = 5
            _ = try? await URLSession.shared.data(for: req)
        }
    }

    private func wifiPostJSON(_ host: String, _ path: String, body: some Encodable) {
        Task {
            guard let url = URL(string: "http://\(host)\(path)") else { return }
            var req = URLRequest(url: url)
            req.httpMethod = "POST"
            req.setValue("application/json", forHTTPHeaderField: "Content-Type")
            req.httpBody = try? JSONEncoder().encode(body)
            req.timeoutInterval = 5
            _ = try? await URLSession.shared.data(for: req)
        }
    }
}
