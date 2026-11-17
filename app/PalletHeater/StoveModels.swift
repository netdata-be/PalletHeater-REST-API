import SwiftUI

enum StoveStatus: UInt8 {
    case off = 0
    case starting = 1
    case palletLoading = 2
    case ignition = 3
    case working = 4
    case brazierCleaning = 5
    case finalCleaning = 6
    case standby = 7
    case alarm = 8
    case ignitionFailure = 9
    case alarmState = 10
    case unknown = 255

    init(raw: UInt8) {
        self = StoveStatus(rawValue: raw) ?? .unknown
    }

    var label: String {
        switch self {
        case .off:             return "Off"
        case .starting:        return "Starting"
        case .palletLoading:   return "Loading Pellets"
        case .ignition:        return "Ignition"
        case .working:         return "Working"
        case .brazierCleaning: return "Cleaning Brazier"
        case .finalCleaning:   return "Final Cleaning"
        case .standby:         return "Standby"
        case .alarm:           return "Alarm"
        case .ignitionFailure: return "Ignition Failure"
        case .alarmState:      return "Alarm State"
        case .unknown:         return "Unknown"
        }
    }

    var isOn: Bool {
        switch self {
        case .off, .unknown: return false
        default: return true
        }
    }

    var isAlarm: Bool {
        switch self {
        case .alarm, .alarmState, .ignitionFailure: return true
        default: return false
        }
    }

    var color: Color {
        switch self {
        case .working:                         return .green
        case .alarm, .alarmState,
             .ignitionFailure:                 return .red
        case .off:                             return .gray
        case .standby:                         return .blue
        default:                               return .orange
        }
    }
}

// Combustion raw 2..8 → adj -3..+3; raw 76 → Eco
enum CombustionMode: Equatable {
    case adjusted(Int)  // -3 to +3
    case eco

    init(raw: UInt8) {
        if raw == 76 { self = .eco }
        else { self = .adjusted(min(3, max(-3, Int(raw) - 5))) }
    }

    var raw: UInt8 {
        switch self {
        case .eco:            return 76
        case .adjusted(let v): return UInt8(clamping: v + 5)
        }
    }

    var label: String {
        switch self {
        case .eco:             return "Eco"
        case .adjusted(let v): return v > 0 ? "+\(v)" : "\(v)"
        }
    }

    static let allCases: [CombustionMode] = [
        .adjusted(-3), .adjusted(-2), .adjusted(-1),
        .adjusted(0),
        .adjusted(1), .adjusted(2), .adjusted(3),
        .eco
    ]
}

// Pellet raw 0..10 → adj -5..+5
func pelletAdj(from raw: UInt8) -> Int { Int(raw) - 5 }
func pelletRaw(from adj: Int) -> UInt8 { UInt8(clamping: adj + 5) }

struct StovePreviousError: Identifiable, Equatable {
    let slot: Int
    let code: Int

    var id: Int { slot }

    var label: String {
        switch code {
        case 0:   return "No Error"
        case 1:   return "Black out"
        case 2:   return "Fume probe"
        case 4:   return "Lighting failed"
        case 8:   return "Safety thermostat"
        case 16:  return "Fume fan encoder"
        case 32:  return "No pellets"
        case 64:  return "Pressure switch"
        case 128: return "Service"
        case 129: return "Ignition failure"
        case 130: return "Pellet safety"
        case 132: return "Fume temperature"
        case 136: return "Door open"
        case 255: return "Unknown"
        default:  return "Code \(code)"
        }
    }
}
