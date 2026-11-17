import SwiftUI

struct ScheduleView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var editingIndex: Int? = nil

    var body: some View {
        NavigationStack {
            List {
                if ble.hasPendingCommand || !ble.commandStatus.isEmpty {
                    Section {
                        HStack(spacing: 10) {
                            if ble.hasPendingCommand {
                                ProgressView()
                            } else {
                                Image(systemName: "exclamationmark.triangle.fill")
                                    .foregroundStyle(.orange)
                            }
                            Text(ble.commandStatus.isEmpty ? "Applying schedule" : ble.commandStatus)
                                .font(.subheadline)
                        }
                    }
                }
                ForEach(Array(ble.timers.enumerated()), id: \.offset) { idx, timer in
                    timerRow(timer, index: idx)
                }
            }
            .navigationTitle("Schedule")
            .navigationBarTitleDisplayMode(.large)
            .sheet(item: Binding(
                get: { editingIndex.map { ScheduleIndex(value: $0) } },
                set: { editingIndex = $0?.value }
            )) { item in
                TimerDetailView(
                    timer: ble.timers[item.value],
                    title: "PR\(item.value + 1)"
                ) { updated in
                    ble.setTimer(updated, at: item.value)
                    editingIndex = nil
                } onCancel: {
                    editingIndex = nil
                }
            }
        }
    }

    @ViewBuilder
    private func timerRow(_ timer: StoveTimer, index: Int) -> some View {
        HStack(spacing: 12) {
            Button {
                var t = timer
                t.enabled = !t.enabled
                if t.enabled {
                    if t.on == nil  { t.on  = "06:00" }
                    if t.off == nil { t.off = "22:00" }
                    if t.settings == nil { t.settings = TimerSettings(setpoint: 20, flame_power: 2, RoomFanSpeed: 1) }
                }
                ble.setTimer(t, at: index)
            } label: {
                Image(systemName: timer.enabled ? "checkmark.circle.fill" : "circle")
                    .foregroundStyle(timer.enabled ? .orange : .secondary)
                    .font(.title3)
            }
            .buttonStyle(.borderless)
            .disabled(ble.isTimerPending(index))

            Button { editingIndex = index } label: {
                HStack(spacing: 12) {
                    VStack(alignment: .leading, spacing: 3) {
                        HStack(spacing: 6) {
                            Text("PR\(index + 1)")
                                .font(.headline)
                                .foregroundStyle(timer.enabled ? .primary : .secondary)
                            if !timer.enabled {
                                Text("Disabled")
                                    .font(.caption2.bold())
                                    .foregroundStyle(.secondary)
                                    .padding(.horizontal, 5)
                                    .padding(.vertical, 2)
                                    .background(Color(.systemFill), in: Capsule())
                            }
                        }
                        if let on = timer.on, let off = timer.off {
                            Text("\(on) – \(off)")
                                .font(.subheadline)
                                .foregroundStyle(timer.enabled ? .secondary : .tertiary)
                        }
                        if let days = timer.enabledOn, !days.shortLabel.isEmpty {
                            Text(days.shortLabel)
                                .font(.caption)
                                .foregroundStyle(timer.enabled ? .secondary : .tertiary)
                        }
                        if let s = timer.settings {
                            HStack(spacing: 8) {
                                Label("\(s.setpoint)°C", systemImage: "thermometer.medium")
                                Label("Flame \(s.flame_power)", systemImage: "flame")
                                Label(s.RoomFanSpeed == 0 ? "Fan off" : "Fan \(s.RoomFanSpeed)", systemImage: "wind")
                            }
                            .font(.caption2)
                            .foregroundStyle(timer.enabled ? .secondary : .tertiary)
                        }
                    }
                    Spacer()
                    if ble.isTimerPending(index) { ProgressView() }
                    Image(systemName: "chevron.right")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(ble.isTimerPending(index))
        }
        .swipeActions(edge: .trailing) {
            if timer.enabled {
                Button(role: .destructive) {
                    var t = timer; t.enabled = false; ble.setTimer(t, at: index)
                } label: { Label("Disable", systemImage: "xmark.circle") }
            } else {
                Button {
                    var t = timer; t.enabled = true
                    if t.on == nil  { t.on  = "06:00" }
                    if t.off == nil { t.off = "22:00" }
                    if t.settings == nil { t.settings = TimerSettings(setpoint: 20, flame_power: 2, RoomFanSpeed: 1) }
                    ble.setTimer(t, at: index)
                } label: { Label("Enable", systemImage: "checkmark.circle") }
                    .tint(.orange)
            }
        }
    }
}

private struct ScheduleIndex: Identifiable {
    let value: Int
    var id: Int { value }
}
