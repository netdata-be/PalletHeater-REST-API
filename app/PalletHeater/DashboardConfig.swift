import Foundation

struct DashboardConfig {
    static let showRemoteTemp = "dashboardShowRemoteTemp"
    static let showPelletQuality = "dashboardShowPelletQuality"
    static let showEnergySaving = "dashboardShowEnergySaving"
    static let showServiceHours = "dashboardShowServiceHours"
    static let showStoveTime = "dashboardShowStoveTime"
    static let showSystemInfo = "dashboardShowSystemInfo"
    static let showPreviousErrors = "dashboardShowPreviousErrors"

    static func dashboardValue(for key: String, default defaultValue: Bool = true) -> Bool {
        guard let stored = UserDefaults.standard.object(forKey: key) as? Bool else {
            return defaultValue
        }
        return stored
    }
}
