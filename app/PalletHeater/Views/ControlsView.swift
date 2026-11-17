import SwiftUI

struct ControlsView: View {
    @EnvironmentObject var ble: BLEManager
    @AppStorage(DashboardConfig.showRemoteTemp) private var showRemoteTemp = true
    @AppStorage(DashboardConfig.showPelletQuality) private var showPelletQuality = true
    @AppStorage(DashboardConfig.showEnergySaving) private var showEnergySaving = true
    @AppStorage(DashboardConfig.showServiceHours) private var showServiceHours = true
    @AppStorage(DashboardConfig.showStoveTime) private var showStoveTime = true
    @AppStorage(DashboardConfig.showSystemInfo) private var showSystemInfo = true
    @AppStorage(DashboardConfig.showPreviousErrors) private var showPreviousErrors = true

    private var isTransitioning: Bool {
        switch ble.stoveStatus {
        case .starting, .palletLoading, .ignition, .brazierCleaning, .finalCleaning: return true
        default: return false
        }
    }

    var body: some View {
        NavigationStack {
            Form {
                if ble.hasPendingCommand || !ble.commandStatus.isEmpty {
                    commandStatusSection
                }
                powerSection
                temperatureSection
                energySavingSection
                fanSection
                flameSection
                preferencesSection
                dashboardWidgetsSection
                advancedSection
            }
            .navigationTitle("Controls")
            .navigationBarTitleDisplayMode(.large)
        }
    }

    // MARK: - Sections

    private var commandStatusSection: some View {
        Section {
            HStack(spacing: 10) {
                if ble.hasPendingCommand {
                    ProgressView()
                } else {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange)
                }
                Text(ble.commandStatus.isEmpty ? "Applying changes" : ble.commandStatus)
                    .font(.subheadline)
                Spacer()
            }
        }
    }

    private var powerSection: some View {
        Section {
            Button {
                ble.setPower(!ble.isPowered)
            } label: {
                HStack {
                    Spacer()
                    Label(
                        ble.isPowered ? "Turn Off" : "Turn On",
                        systemImage: ble.isPowered ? "power.circle.fill" : "power.circle"
                    )
                    .font(.title3.bold())
                    .foregroundColor(isTransitioning ? .secondary : (ble.isPowered ? .red : .green))
                    Spacer()
                }
                .padding(.vertical, 6)
            }
            .disabled(isTransitioning || ble.isPowerPending)
        } footer: {
            Text(ble.isPowerPending ? "Waiting for stove confirmation" : ble.stoveStatus.label)
                .foregroundStyle(ble.stoveStatus.color)
        }
    }

    private var temperatureSection: some View {
        Section("Temperature Setpoint") {
            HStack {
                Label("\(ble.setpointTemp) °C", systemImage: "thermometer.medium")
                Spacer()
                Stepper("", value: Binding(
                    get: { ble.setpointTemp },
                    set: { ble.setTemperature($0) }
                ), in: 7...30)
                .labelsHidden()
                .disabled(ble.isSetpointPending)
            }
            Text(ble.isSetpointPending ? "Applying setpoint…" : "Range: 7 – 30 °C")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var energySavingSection: some View {
        Section("Energy Saving") {
            Toggle(isOn: Binding(
                get: { ble.energySavingEnabled },
                set: { ble.setEnergySavingEnabled($0) }
            )) {
                Label("Energy Saving", systemImage: "leaf.fill")
            }
            .disabled(ble.isEnergySavingPending)

            HStack {
                Label("Start", systemImage: "arrow.down.circle")
                Spacer()
                Text("\(offsetText(ble.energySavingStartOffset)) - \(ble.energySavingStartTemperature) °C")
                    .font(.callout.bold())
            }
            Stepper("", value: Binding(
                get: { ble.energySavingStartTemperature },
                set: { ble.setEnergySaving(startTemperature: $0, stopTemperature: ble.energySavingStopTemperature) }
            ), in: 7...ble.setpointTemp)
            .labelsHidden()
            .disabled(!ble.energySavingEnabled || ble.isEnergySavingPending)

            HStack {
                Label("Stop", systemImage: "arrow.up.circle")
                Spacer()
                Text("\(offsetText(ble.energySavingStopOffset)) - \(ble.energySavingStopTemperature) °C")
                    .font(.callout.bold())
            }
            Stepper("", value: Binding(
                get: { ble.energySavingStopTemperature },
                set: { ble.setEnergySaving(startTemperature: ble.energySavingStartTemperature, stopTemperature: $0) }
            ), in: ble.setpointTemp...33)
            .labelsHidden()
            .disabled(!ble.energySavingEnabled || ble.isEnergySavingPending)

            if ble.isEnergySavingPending {
                pendingText("Updating energy-saving…")
            }
            if !ble.energySavingStatus.isEmpty {
                Text(ble.energySavingStatus)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Text("Current range: \(ble.energySavingStartTemperature) – \(ble.energySavingStopTemperature) °C")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var fanSection: some View {
        Section("Room Fan") {
            Picker("Fan Speed", selection: Binding(
                get: { ble.fanSpeed },
                set: { ble.setFan($0) }
            )) {
                Text("Off").tag(0)
                Text("1").tag(1)
                Text("2").tag(2)
                Text("3").tag(3)
            }
            .pickerStyle(.segmented)
            .disabled(ble.isFanPending)
            if ble.isFanPending {
                pendingText("Applying fan speed…")
            }
        }
    }

    private var flameSection: some View {
        Section("Flame Power") {
            Picker("Flame Level", selection: Binding(
                get: { ble.flameLevel },
                set: { ble.setFlame($0) }
            )) {
                Text("1").tag(1)
                Text("2").tag(2)
                Text("3").tag(3)
                Text("4").tag(4)
            }
            .pickerStyle(.segmented)
            .disabled(ble.isFlamePending)
            if ble.isFlamePending {
                pendingText("Applying flame power…")
            }
        }
    }

    private var preferencesSection: some View {
        Section("Preferences") {
            Toggle(isOn: Binding(
                get: { ble.beepEnabled },
                set: { ble.setBeep($0) }
            )) {
                Label("Beep", systemImage: "speaker.wave.2.fill")
            }
            .disabled(ble.isBeepPending)
            if ble.isBeepPending {
                pendingText("Applying beep setting…")
            }
        }
    }

    private var dashboardWidgetsSection: some View {
        Section("Dashboard Widgets") {
            Toggle("Remote temperature", isOn: Binding(
                get: { showRemoteTemp },
                set: { newValue in showRemoteTemp = newValue; ble.refreshDashboardInfoIfNeeded() }
            ))
            Toggle("Pellet quality", isOn: $showPelletQuality)
            Toggle("Energy saving", isOn: $showEnergySaving)
            Toggle("Service hours", isOn: Binding(
                get: { showServiceHours },
                set: { newValue in showServiceHours = newValue; ble.refreshDashboardInfoIfNeeded() }
            ))
            Toggle("Stove time", isOn: Binding(
                get: { showStoveTime },
                set: { newValue in showStoveTime = newValue; ble.refreshDashboardInfoIfNeeded() }
            ))
            Toggle("System info", isOn: Binding(
                get: { showSystemInfo },
                set: { newValue in showSystemInfo = newValue; ble.refreshDashboardInfoIfNeeded() }
            ))
            Toggle("Previous errors", isOn: Binding(
                get: { showPreviousErrors },
                set: { newValue in showPreviousErrors = newValue; ble.refreshDashboardInfoIfNeeded() }
            ))
        }
    }

    private var advancedSection: some View {
        Section {
            combustionPicker
            pelletQualityPicker
        } header: {
            Text("Advanced")
        } footer: {
            Text("Combustion and pellet quality adjustments affect burn efficiency. Positive values increase air/fuel respectively.")
        }
    }

    private var combustionPicker: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Combustion Quality", systemImage: "airflow")
                .font(.subheadline)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(CombustionMode.allCases, id: \.label) { mode in
                        let selected = (ble.combustionMode == mode)
                        Button {
                            ble.setCombustion(mode)
                        } label: {
                            Text(mode.label)
                                .font(.callout.bold())
                                .padding(.horizontal, 14)
                                .padding(.vertical, 8)
                                .background(selected ? Color.orange : Color(.systemFill),
                                            in: RoundedRectangle(cornerRadius: 10))
                                .foregroundStyle(selected ? .white : .primary)
                        }
                        .buttonStyle(.plain)
                        .disabled(ble.isCombustionPending)
                    }
                }
                .padding(.vertical, 2)
            }
            if ble.isCombustionPending {
                pendingText("Applying combustion setting…")
            }
        }
        .padding(.vertical, 4)
    }

    private var pelletQualityPicker: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Pellet Quality", systemImage: "circle.hexagonpath.fill")
                .font(.subheadline)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(-5...5, id: \.self) { value in
                        let selected = ble.pelletAdjValue == value
                        Button {
                            ble.setPellet(value)
                        } label: {
                            Text(value > 0 ? "+\(value)" : "\(value)")
                                .font(.callout.bold())
                                .padding(.horizontal, 12)
                                .padding(.vertical, 8)
                                .background(selected ? Color.orange : Color(.systemFill), in: RoundedRectangle(cornerRadius: 10))
                                .foregroundStyle(selected ? .white : .primary)
                        }
                        .buttonStyle(.plain)
                        .disabled(ble.isPelletPending)
                    }
                }
                .padding(.vertical, 2)
            }
            if ble.isPelletPending {
                pendingText("Applying pellet setting…")
            }
        }
        .padding(.vertical, 4)
    }

    private func pendingText(_ text: String) -> some View {
        HStack(spacing: 8) {
            ProgressView()
            Text(text)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private func offsetText(_ offset: Int) -> String {
        offset >= 0 ? "+\(offset)°C" : "\(offset)°C"
    }
}
