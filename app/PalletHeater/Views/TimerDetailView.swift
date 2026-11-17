import SwiftUI

struct TimerDetailView: View {
    let title: String
    let onSave: (StoveTimer) -> Void
    let onCancel: () -> Void

    @State private var timer: StoveTimer
    @State private var onTime: Date
    @State private var offTime: Date

    init(timer: StoveTimer, title: String, onSave: @escaping (StoveTimer) -> Void, onCancel: @escaping () -> Void) {
        self.title = title
        self.onSave = onSave
        self.onCancel = onCancel
        _timer = State(initialValue: timer)
        _onTime = State(initialValue: Self.parseTime(timer.on ?? "06:00"))
        _offTime = State(initialValue: Self.parseTime(timer.off ?? "22:00"))
    }

    var body: some View {
        NavigationStack {
            Form {
                enableSection
                timeSection
                daysSection
                settingsSection
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel", action: onCancel)
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Save") { saveAndClose() }
                        .bold()
                }
            }
        }
    }

    private var enableSection: some View {
        Section {
            Toggle("Enabled", isOn: $timer.enabled)
        }
    }

    private var timeSection: some View {
        Section("Time") {
            DatePicker("Turn On", selection: $onTime, displayedComponents: .hourAndMinute)
            DatePicker("Turn Off", selection: $offTime, displayedComponents: .hourAndMinute)
        }
    }

    private var daysSection: some View {
        Section("Days") {
            let days: [(String, WritableKeyPath<DaysOfWeek, Bool>)] = [
                ("Mon", \.monday), ("Tue", \.tuesday), ("Wed", \.wednesday),
                ("Thu", \.thursday), ("Fri", \.friday), ("Sat", \.saturday), ("Sun", \.sunday)
            ]
            HStack(spacing: 6) {
                ForEach(days, id: \.0) { label, kp in
                    let isOn = timer.enabledOn?[keyPath: kp] ?? false
                    Button {
                        if timer.enabledOn == nil {
                            timer.enabledOn = DaysOfWeek()
                        }
                        timer.enabledOn![keyPath: kp] = !isOn
                    } label: {
                        Text(label)
                            .font(.caption.bold())
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 7)
                            .background(isOn ? Color.orange : Color(.systemFill),
                                        in: RoundedRectangle(cornerRadius: 8))
                            .foregroundStyle(isOn ? .white : .primary)
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.vertical, 2)
        }
    }

    private var settingsSection: some View {
        Section("Stove Settings") {
            HStack {
                Label("Setpoint", systemImage: "thermometer.medium")
                Spacer()
                Stepper("\(timer.settings?.setpoint ?? 20) °C",
                        value: Binding(
                            get: { timer.settings?.setpoint ?? 20 },
                            set: { v in mutateSettings { $0.setpoint = v } }
                        ),
                        in: 7...30)
            }
            VStack(alignment: .leading, spacing: 6) {
                Label("Flame Power", systemImage: "flame")
                    .font(.subheadline)
                Picker("Flame", selection: Binding(
                    get: { timer.settings?.flame_power ?? 2 },
                    set: { v in mutateSettings { $0.flame_power = v } }
                )) {
                    ForEach(1...4, id: \.self) { Text("\($0)").tag($0) }
                }
                .pickerStyle(.segmented)
            }
            .padding(.vertical, 2)
            VStack(alignment: .leading, spacing: 6) {
                Label("Fan Speed", systemImage: "fan")
                    .font(.subheadline)
                Picker("Fan", selection: Binding(
                    get: { timer.settings?.RoomFanSpeed ?? 1 },
                    set: { v in mutateSettings { $0.RoomFanSpeed = v } }
                )) {
                    Text("Off").tag(0)
                    ForEach(1...3, id: \.self) { Text("\($0)").tag($0) }
                }
                .pickerStyle(.segmented)
            }
            .padding(.vertical, 2)
        }
    }

    private func mutateSettings(_ update: (inout TimerSettings) -> Void) {
        var s = timer.settings ?? TimerSettings(setpoint: 20, flame_power: 2, RoomFanSpeed: 1)
        update(&s)
        timer.settings = s
    }

    private func saveAndClose() {
        timer.on = Self.formatTime(onTime)
        timer.off = Self.formatTime(offTime)
        onSave(timer)
    }

    private static func parseTime(_ string: String) -> Date {
        let parts = string.split(separator: ":").compactMap { Int($0) }
        var components = Calendar.current.dateComponents([.year, .month, .day], from: Date())
        components.hour = parts.count > 0 ? parts[0] : 6
        components.minute = parts.count > 1 ? parts[1] : 0
        return Calendar.current.date(from: components) ?? Date()
    }

    private static func formatTime(_ date: Date) -> String {
        let c = Calendar.current.dateComponents([.hour, .minute], from: date)
        let h = c.hour ?? 0
        let m = ((c.minute ?? 0) / 10) * 10
        return String(format: "%02d:%02d", h, m)
    }
}
