import SwiftUI

struct DashboardView: View {
    @EnvironmentObject var ble: BLEManager
    @AppStorage(DashboardConfig.showRemoteTemp)    private var showRemoteTemp    = true
    @AppStorage(DashboardConfig.showPelletQuality) private var showPelletQuality = true
    @AppStorage(DashboardConfig.showEnergySaving)  private var showEnergySaving  = true
    @AppStorage(DashboardConfig.showSystemInfo)    private var showSystemInfo    = true
    @AppStorage(DashboardConfig.showPreviousErrors) private var showPreviousErrors = true
    @State private var showSetpointEditor = false
    @State private var editingSetpoint = 20
    @State private var showFlameEditor = false
    @State private var editingFlame = 2
    @State private var showFanEditor = false
    @State private var editingFan = 1
    @State private var showPowerConfirm = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if ble.lowPallet {
                        lowPalletAlertBanner
                    }
                    if ble.isPopulating {
                        loadingProgressCard
                    }
                    statusCard
                    temperatureRow
                    HStack(spacing: 16) {
                        infoTile(
                            title: "Fume Temp",
                            value: "\(ble.fumeTemp) °C",
                            icon: "thermometer.high",
                            color: .red
                        )
                        infoTile(
                            title: "Fan RPM",
                            value: ble.fumeFanRPM > 0 ? "\(ble.fumeFanRPM)" : "—",
                            icon: "fan.fill",
                            color: .blue
                        )
                    }
                    if showRemoteTemp || showPelletQuality {
                        HStack(spacing: 16) {
                            if showRemoteTemp {
                                infoTile(
                                    title: "Remote Temp",
                                    value: ble.remoteControlTemp.map { String(format: "%.1f °C", $0) } ?? "—",
                                    icon: "thermometer.medium",
                                    color: .blue
                                )
                            }
                            if showPelletQuality {
                                infoTile(
                                    title: "Pellet Quality",
                                    value: ble.pelletAdjValue > 0 ? "+\(ble.pelletAdjValue)" : "\(ble.pelletAdjValue)",
                                    icon: "circle.hexagonpath.fill",
                                    color: .brown
                                )
                            }
                        }
                    }
                    HStack(spacing: 16) {
                        infoTile(
                            title: "Flame",
                            value: "Level \(ble.flameLevel)",
                            icon: "flame.fill",
                            color: .orange,
                            hint: "Hold to edit"
                        )
                        .onLongPressGesture {
                            editingFlame = ble.flameLevel
                            showFlameEditor = true
                        }
                        infoTile(
                            title: "Fan Speed",
                            value: ble.fanSpeed == 0 ? "Off" : "Speed \(ble.fanSpeed)",
                            icon: "wind",
                            color: .cyan,
                            hint: "Hold to edit"
                        )
                        .onLongPressGesture {
                            editingFan = ble.fanSpeed
                            showFanEditor = true
                        }
                    }
                    if showEnergySaving && ble.energySavingEnabled {
                        energySavingTile
                    }
                    if ble.lowPallet {
                        lowPalletWarning
                    }
                    if showSystemInfo {
                        stoveInfoSection
                    }
                    if showPreviousErrors {
                        previousErrorsSection
                    }
                }
                .padding()
            }
            .navigationTitle("Dashboard")
            .navigationBarTitleDisplayMode(.large)
            .refreshable { await ble.forceRefresh() }
        }
        .sheet(isPresented: $showSetpointEditor) {
            SetpointEditor(value: $editingSetpoint) {
                ble.setTemperature(editingSetpoint)
                showSetpointEditor = false
            }
            .presentationDetents([.medium])
        }
        .sheet(isPresented: $showFlameEditor) {
            LevelEditor(
                title: "Flame Power",
                icon: "flame.fill",
                tint: .orange,
                value: $editingFlame,
                options: [(label: "1", value: 1), (label: "2", value: 2), (label: "3", value: 3), (label: "4", value: 4)]
            ) {
                ble.setFlame(editingFlame)
                showFlameEditor = false
            }
            .presentationDetents([.medium])
        }
        .sheet(isPresented: $showFanEditor) {
            LevelEditor(
                title: "Fan Speed",
                icon: "wind",
                tint: .cyan,
                value: $editingFan,
                options: [(label: "Off", value: 0), (label: "1", value: 1), (label: "2", value: 2), (label: "3", value: 3)]
            ) {
                ble.setFan(editingFan)
                showFanEditor = false
            }
            .presentationDetents([.medium])
        }
        .alert(ble.isPowered ? "Turn Off Stove?" : "Turn On Stove?", isPresented: $showPowerConfirm) {
            Button(ble.isPowered ? "Turn Off" : "Turn On", role: ble.isPowered ? .destructive : nil) {
                ble.setPower(!ble.isPowered)
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(ble.isPowered ? "The stove will begin its shutdown cycle." : "The stove will begin its startup cycle.")
        }
    }

    private var statusCard: some View {
        HStack(spacing: 16) {
            VStack(spacing: 2) {
                ZStack {
                    Circle()
                        .fill(ble.stoveStatus.color.opacity(0.15))
                        .frame(width: 56, height: 56)
                    Image(systemName: statusIcon)
                        .font(.title2)
                        .foregroundStyle(ble.stoveStatus.color)
                }
                Text("Hold")
                    .font(.system(size: 9))
                    .foregroundStyle(.tertiary)
            }
            .onLongPressGesture { showPowerConfirm = true }
            VStack(alignment: .leading, spacing: 4) {
                Text(ble.stoveStatus.label)
                    .font(.title3.bold())
                Text(ble.isPowered ? "Powered on" : "Powered off")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                if !ble.ipAddress.isEmpty {
                    Label(ble.ipAddress, systemImage: "network")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
                if ble.rssi != nil {
                    Label(ble.signalDescription, systemImage: ble.signalIconName)
                        .font(.caption2.bold())
                        .foregroundStyle(signalColor)
                }
            }
            Spacer()
            Circle()
                .fill(ble.stoveStatus.color)
                .frame(width: 12, height: 12)
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private var temperatureRow: some View {
        HStack(spacing: 16) {
            VStack(alignment: .leading, spacing: 4) {
                Label("Room", systemImage: "thermometer.medium")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text(String(format: "%.1f °C", ble.roomTemp))
                    .font(.system(size: 38, weight: .bold, design: .rounded))
                    .foregroundStyle(.primary)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 4) {
                Label("Setpoint", systemImage: "target")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("\(ble.setpointTemp) °C")
                    .font(.system(size: 38, weight: .bold, design: .rounded))
                    .foregroundStyle(.orange)
                Text("Hold to edit")
                    .font(.system(size: 9))
                    .foregroundStyle(.tertiary)
            }
            .onLongPressGesture {
                editingSetpoint = ble.setpointTemp
                showSetpointEditor = true
            }
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private func infoTile(title: String, value: String, icon: String, color: Color, hint: String? = nil) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Label(title, systemImage: icon)
                .font(.caption)
                .foregroundStyle(color)
            Text(value)
                .font(.title2.bold())
            if let hint {
                Text(hint)
                    .font(.system(size: 9))
                    .foregroundStyle(.tertiary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private var energySavingTile: some View {
        infoTile(
            title: "Energy Saving",
            value: "\(ble.energySavingStartTemperature)–\(ble.energySavingStopTemperature) °C",
            icon: "leaf.fill",
            color: .green
        )
    }

    private var lowPalletWarning: some View {
        HStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.red)
            Text("Low pellet alarm — refill required")
                .font(.subheadline.bold())
            Spacer()
        }
        .padding()
        .background(Color.red.opacity(0.14), in: RoundedRectangle(cornerRadius: 16))
    }

    private var lowPalletAlertBanner: some View {
        HStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.white)
            VStack(alignment: .leading, spacing: 2) {
                Text("Low pellets alarm")
                    .font(.subheadline.weight(.bold))
                    .foregroundColor(.white)
                Text("Refill required to avoid a shutdown")
                    .font(.caption)
                    .foregroundColor(.white.opacity(0.85))
            }
            Spacer()
        }
        .padding()
        .background(Color.red.gradient, in: RoundedRectangle(cornerRadius: 16))
    }

    private var loadingProgressCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                ProgressView(value: ble.initialReadTotalCount > 0 ?
                             Double(ble.initialReadTotalCount - ble.pendingInitialReadCount) / Double(ble.initialReadTotalCount) : nil)
                    .progressViewStyle(.linear)
                Spacer()
            }
            Text(ble.initialReadProgressText)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private var stoveInfoSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Stove Info", systemImage: "info.circle")
                .font(.headline)
            HStack(spacing: 12) {
                infoValue("Firmware", ble.firmwareVersion.isEmpty ? "—" : ble.firmwareVersion)
                infoValue("Runtime", ble.totalRuntime > 0 ? "\(ble.totalRuntime) h" : "—")
            }
            HStack(spacing: 12) {
                infoValue("Starts", ble.numberOfStarts > 0 ? "\(ble.numberOfStarts)" : "—")
                infoValue("Service", ble.hoursSinceLastService != nil ? "\(ble.hoursSinceLastService!) h" : "—")
            }
            HStack(spacing: 12) {
                infoValue("Stove Time", ble.stoveTimeText)
                infoValue("Energy Saving", ble.energySavingEnabled ? "On" : "Off")
            }
            HStack(spacing: 12) {
                infoValue("Target", ble.energySavingEnabled ? "\(ble.energySavingStartTemperature) – \(ble.energySavingStopTemperature) °C" : "—")
                infoValue("Low Pellets Alarm", ble.lowPallet ? "Yes" : "No")
            }
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private var previousErrorsSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Previous Errors", systemImage: "exclamationmark.triangle")
                .font(.headline)
                .padding(.horizontal)
                .padding(.top)
            if ble.previousErrors.isEmpty {
                Text("No previous errors")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .padding(.horizontal)
                    .padding(.bottom)
            } else {
                VStack(spacing: 0) {
                    ForEach(ble.previousErrors) { error in
                        ErrorRow(error: error) {
                            withAnimation { ble.removePreviousError(id: error.id) }
                        }
                        if error.id != ble.previousErrors.last?.id {
                            Divider().padding(.leading)
                        }
                    }
                }
                .padding(.bottom, 4)
            }
        }
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private func infoValue(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.headline)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var statusIcon: String {
        switch ble.stoveStatus {
        case .working:                   return "flame.fill"
        case .alarm, .alarmState,
             .ignitionFailure:           return "exclamationmark.triangle.fill"
        case .off:                       return "power"
        case .standby:                   return "moon.fill"
        case .starting, .ignition:       return "flame"
        case .palletLoading:             return "arrow.down.circle"
        case .brazierCleaning,
             .finalCleaning:            return "sparkles"
        default:                         return "questionmark.circle"
        }
    }

    private var signalColor: Color {
        switch ble.signalQualityLabel {
        case "Good": return .green
        case "Okay": return .orange
        case "Bad":  return .red
        default:     return .secondary
        }
    }
}

private struct SetpointEditor: View {
    @Binding var value: Int
    let onApply: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            VStack(spacing: 40) {
                Spacer()
                HStack(spacing: 28) {
                    Button {
                        if value > 7 { value -= 1 }
                    } label: {
                        Image(systemName: "minus.circle.fill")
                            .font(.system(size: 52))
                            .foregroundStyle(value > 7 ? .orange : .secondary)
                    }
                    .disabled(value <= 7)

                    Text("\(value) °C")
                        .font(.system(size: 64, weight: .bold, design: .rounded))
                        .foregroundStyle(.orange)
                        .frame(minWidth: 160)
                        .contentTransition(.numericText())
                        .animation(.default, value: value)

                    Button {
                        if value < 30 { value += 1 }
                    } label: {
                        Image(systemName: "plus.circle.fill")
                            .font(.system(size: 52))
                            .foregroundStyle(value < 30 ? .orange : .secondary)
                    }
                    .disabled(value >= 30)
                }
                Text("Range: 7 – 30 °C")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button(action: onApply) {
                    Text("Apply")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding()
                        .background(Color.orange, in: RoundedRectangle(cornerRadius: 16))
                        .foregroundStyle(.white)
                }
                .padding(.horizontal)
                .padding(.bottom)
            }
            .navigationTitle("Set Temperature")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
    }
}

private struct ErrorRow: View {
    let error: StovePreviousError
    let onDelete: () -> Void

    @State private var dragOffset: CGFloat = 0
    private let deleteWidth: CGFloat = 64

    var body: some View {
        ZStack(alignment: .trailing) {
            Button(action: {
                withAnimation(.easeInOut(duration: 0.2)) { dragOffset = 0 }
                onDelete()
            }) {
                Image(systemName: "trash")
                    .foregroundColor(.white)
                    .frame(width: deleteWidth)
                    .frame(maxHeight: .infinity)
            }
            .background(Color.red)

            HStack {
                Text("Memory \(error.slot)")
                    .font(.subheadline.bold())
                Spacer()
                Text(error.label)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal)
            .padding(.vertical, 10)
            .background(Color(.systemBackground))
            .offset(x: dragOffset)
            .gesture(
                DragGesture(minimumDistance: 10)
                    .onChanged { value in
                        if value.translation.width < 0 {
                            dragOffset = max(value.translation.width, -deleteWidth)
                        } else if dragOffset < 0 {
                            dragOffset = min(0, dragOffset + value.translation.width)
                        }
                    }
                    .onEnded { value in
                        withAnimation(.easeInOut(duration: 0.25)) {
                            dragOffset = value.translation.width < -(deleteWidth / 2) ? -deleteWidth : 0
                        }
                    }
            )
        }
        .clipped()
    }
}

private struct LevelEditor: View {
    let title: String
    let icon: String
    let tint: Color
    @Binding var value: Int
    let options: [(label: String, value: Int)]
    let onApply: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            VStack(spacing: 32) {
                Spacer()
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 16), count: options.count > 3 ? 2 : options.count), spacing: 16) {
                    ForEach(options, id: \.value) { option in
                        Button {
                            value = option.value
                        } label: {
                            Text(option.label)
                                .font(.title.bold())
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 28)
                                .background(value == option.value ? tint : Color(.systemFill),
                                            in: RoundedRectangle(cornerRadius: 16))
                                .foregroundStyle(value == option.value ? .white : .primary)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal)
                Spacer()
                Button(action: onApply) {
                    Text("Apply")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding()
                        .background(tint, in: RoundedRectangle(cornerRadius: 16))
                        .foregroundStyle(.white)
                }
                .padding(.horizontal)
                .padding(.bottom)
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
    }
}
