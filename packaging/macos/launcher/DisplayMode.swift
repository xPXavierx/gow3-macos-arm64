// SPDX-License-Identifier: GPL-2.0-or-later
// A pipe lease holds a public CoreGraphics, app-scoped display configuration.
// EOF (including a crashed parent) releases it. WindowServer also restores the
// session configuration if this helper itself is killed.
import Foundation
import CoreGraphics
import Darwin

func report(_ value: [String: Any]) {
    if let data = try? JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]) {
        FileHandle.standardOutput.write(data)
        FileHandle.standardOutput.write(Data([10]))
    }
}
func sameSize(_ a: CGDisplayMode, _ b: CGDisplayMode) -> Bool {
    a.width == b.width && a.height == b.height &&
    a.pixelWidth == b.pixelWidth && a.pixelHeight == b.pixelHeight
}
func configure(_ display: CGDirectDisplayID, _ mode: CGDisplayMode) -> CGError {
    var configuration: CGDisplayConfigRef?
    let begin = CGBeginDisplayConfiguration(&configuration)
    guard begin == .success else { return begin }
    let change = CGConfigureDisplayWithDisplayMode(configuration, display, mode, nil)
    guard change == .success else {
        CGCancelDisplayConfiguration(configuration)
        return change
    }
    return CGCompleteDisplayConfiguration(configuration, .forAppOnly)
}

// The fullscreen game starts on the main display. Never change other displays.
let display = CGMainDisplayID()
guard let original = CGDisplayCopyDisplayMode(display) else {
    report(["state": "unavailable", "reason": "no_display"])
    exit(0)
}
let modes = CGDisplayCopyAllDisplayModes(display,
    [kCGDisplayShowDuplicateLowResolutionModes as String: true] as CFDictionary) as? [CGDisplayMode] ?? []
let candidates = modes.filter { sameSize($0, original) && abs($0.refreshRate - 60) < 0.01 }
if abs(original.refreshRate - 60) < 0.01 {
    report(["state": "unchanged", "display": display, "hz": original.refreshRate])
    exit(0)
}
guard let target = candidates.first else {
    report(["state": "unavailable", "reason": "no_matching_60hz_mode", "display": display,
            "original_hz": original.refreshRate])
    exit(0)
}
let change = configure(display, target)
guard change == .success else {
    report(["state": "unavailable", "reason": "configuration_rejected", "error": change.rawValue])
    exit(0)
}
guard let applied = CGDisplayCopyDisplayMode(display), applied.ioDisplayModeID == target.ioDisplayModeID else {
    _ = configure(display, original)
    report(["state": "unavailable", "reason": "configuration_not_applied"])
    exit(0)
}
report(["state": "applied", "display": display, "original_hz": original.refreshRate,
        "hz": applied.refreshRate, "original_mode": original.ioDisplayModeID,
        "mode": applied.ioDisplayModeID, "width": applied.width, "height": applied.height,
        "pixel_width": applied.pixelWidth, "pixel_height": applied.pixelHeight])
// Do not let an interrupt terminate us before explicit restoration. The owning
// session closes the pipe in finally; process death closes it in the kernel.
signal(SIGINT, SIG_IGN)
signal(SIGTERM, SIG_IGN)
var byte: UInt8 = 0
while read(STDIN_FILENO, &byte, 1) > 0 {}
if let current = CGDisplayCopyDisplayMode(display), current.ioDisplayModeID == target.ioDisplayModeID {
    let restored = configure(display, original)
    report(["state": "restored", "error": restored.rawValue, "hz": original.refreshRate,
            "mode": original.ioDisplayModeID])
} else {
    report(["state": "changed_externally"])
}
