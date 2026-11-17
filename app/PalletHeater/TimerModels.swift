import Foundation

struct StoveTimer: Codable {
    var enabled: Bool
    var on: String?
    var off: String?
    var enabledOn: DaysOfWeek?
    var settings: TimerSettings?

    enum CodingKeys: String, CodingKey {
        case enabled, on, off
        case enabledOn = "enabled_on"
        case settings
    }

    static var defaultEnabled: StoveTimer {
        StoveTimer(
            enabled: true,
            on: "06:00",
            off: "22:00",
            enabledOn: DaysOfWeek(monday: true, tuesday: true, wednesday: true,
                                  thursday: true, friday: true, saturday: false, sunday: false),
            settings: TimerSettings(setpoint: 20, flame_power: 2, RoomFanSpeed: 1)
        )
    }
}

struct DaysOfWeek: Codable {
    var monday: Bool = false
    var tuesday: Bool = false
    var wednesday: Bool = false
    var thursday: Bool = false
    var friday: Bool = false
    var saturday: Bool = false
    var sunday: Bool = false

    var shortLabel: String {
        zip(["M","T","W","T","F","S","S"],
            [monday, tuesday, wednesday, thursday, friday, saturday, sunday])
            .filter { $0.1 }.map { $0.0 }.joined()
    }

    var mask: UInt8 {
        var m: UInt8 = 0
        if monday    { m |= 1 }
        if tuesday   { m |= 2 }
        if wednesday { m |= 4 }
        if thursday  { m |= 8 }
        if friday    { m |= 16 }
        if saturday  { m |= 32 }
        if sunday    { m |= 64 }
        return m
    }

}

extension DaysOfWeek {
    init(mask: UInt8) {
        self.init(
            monday:    mask & 1  != 0,
            tuesday:   mask & 2  != 0,
            wednesday: mask & 4  != 0,
            thursday:  mask & 8  != 0,
            friday:    mask & 16 != 0,
            saturday:  mask & 32 != 0,
            sunday:    mask & 64 != 0
        )
    }
}

// BLE wire format: [enabled, on_raw, off_raw, setpoint, flame, fan, dow_mask]
// on_raw/off_raw: val*10 = minutes since 00:00; 144 = disabled
extension StoveTimer {
    var bleBytes: Data {
        var d = Data(count: 7)
        d[0] = enabled ? 1 : 0
        d[1] = Self.timeStrToRaw(on)
        d[2] = Self.timeStrToRaw(off)
        d[3] = UInt8(clamping: settings?.setpoint ?? 20)
        d[4] = UInt8(clamping: settings?.flame_power ?? 2)
        d[5] = UInt8(clamping: settings?.RoomFanSpeed ?? 1)
        d[6] = enabledOn?.mask ?? 0
        return d
    }

    init(bleData: Data) {
        guard bleData.count >= 7 else {
            self.init(enabled: false, on: nil, off: nil, enabledOn: nil, settings: nil)
            return
        }
        self.init(
            enabled:   bleData[0] != 0,
            on:        Self.rawToTimeStr(bleData[1]),
            off:       Self.rawToTimeStr(bleData[2]),
            enabledOn: DaysOfWeek(mask: bleData[6]),
            settings:  TimerSettings(setpoint: Int(bleData[3]), flame_power: Int(bleData[4]), RoomFanSpeed: Int(bleData[5]))
        )
    }

    private static func rawToTimeStr(_ raw: UInt8) -> String? {
        guard raw != 144 else { return nil }
        let mins = Int(raw) * 10
        return String(format: "%02d:%02d", mins / 60, mins % 60)
    }

    private static func timeStrToRaw(_ s: String?) -> UInt8 {
        guard let s else { return 144 }
        let parts = s.split(separator: ":").compactMap { Int($0) }
        guard parts.count >= 2 else { return 144 }
        return UInt8(clamping: (parts[0] * 60 + parts[1]) / 10)
    }
}

struct TimerSettings: Codable {
    var setpoint: Int
    var flame_power: Int
    var RoomFanSpeed: Int
}


