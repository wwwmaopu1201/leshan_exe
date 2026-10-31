import AppKit
import Foundation

let clientRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let source = clientRoot.appendingPathComponent("../../boer-lan-client/src/assets/images/logo.png").standardizedFileURL
let assets = clientRoot.appendingPathComponent("assets")
let output = assets.appendingPathComponent("client-app-icon.png")
let side = 1024
let inset = 82 // Match the padding of the Qt server's app icon.

guard let logo = NSImage(contentsOf: source),
      let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: side, pixelsHigh: side,
                                    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                    isPlanar: false, colorSpaceName: .deviceRGB,
                                    bytesPerRow: 0, bitsPerPixel: 0),
      let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
    fatalError("Could not create the client application icon")
}
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = context
context.cgContext.clear(CGRect(x: 0, y: 0, width: side, height: side))
context.imageInterpolation = .high
logo.draw(in: NSRect(x: inset, y: inset, width: side - inset * 2, height: side - inset * 2),
          from: .zero, operation: .sourceOver, fraction: 1)
context.flushGraphics()
NSGraphicsContext.restoreGraphicsState()
guard let png = bitmap.representation(using: .png, properties: [:]) else {
    fatalError("Could not encode the client application icon")
}
try png.write(to: output)

let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("boer-client-icons-\(UUID().uuidString)")
let iconset = temporary.appendingPathComponent("client.iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: temporary) }

func run(_ executable: String, _ arguments: [String]) throws {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: executable)
    process.arguments = arguments
    try process.run()
    process.waitUntilExit()
    guard process.terminationStatus == 0 else { fatalError("Icon tool failed: \(executable)") }
}

let iconsetSizes: [(String, Int)] = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024)
]
for (name, size) in iconsetSizes {
    try run("/usr/bin/sips", ["-z", String(size), String(size), output.path,
                               "--out", iconset.appendingPathComponent(name).path])
}
func appendBig32(_ value: Int, to data: inout Data) {
    data.append(UInt8((value >> 24) & 0xff))
    data.append(UInt8((value >> 16) & 0xff))
    data.append(UInt8((value >> 8) & 0xff))
    data.append(UInt8(value & 0xff))
}
let icnsParts: [(String, String)] = [
    ("icp4", "icon_16x16.png"), ("icp5", "icon_32x32.png"),
    ("icp6", "icon_32x32@2x.png"), ("ic07", "icon_128x128.png"),
    ("ic08", "icon_256x256.png"), ("ic09", "icon_512x512.png"),
    ("ic10", "icon_512x512@2x.png"), ("ic11", "icon_16x16@2x.png"),
    ("ic12", "icon_32x32@2x.png"), ("ic13", "icon_128x128@2x.png"),
    ("ic14", "icon_256x256@2x.png")
]
var chunks = Data()
for (type, name) in icnsParts {
    let image = try Data(contentsOf: iconset.appendingPathComponent(name))
    chunks.append(contentsOf: type.utf8)
    appendBig32(image.count + 8, to: &chunks)
    chunks.append(image)
}
var icns = Data("icns".utf8)
appendBig32(chunks.count + 8, to: &icns)
icns.append(chunks)
try icns.write(to: assets.appendingPathComponent("client-icon.icns"))

func append16(_ value: Int, to data: inout Data) {
    data.append(UInt8(value & 0xff))
    data.append(UInt8((value >> 8) & 0xff))
}
func append32(_ value: Int, to data: inout Data) {
    append16(value & 0xffff, to: &data)
    append16((value >> 16) & 0xffff, to: &data)
}
let icoSizes = [16, 32, 48, 64, 128, 256]
var images: [(Int, Data)] = []
for size in icoSizes {
    let path = temporary.appendingPathComponent("ico-\(size).png")
    try run("/usr/bin/sips", ["-z", String(size), String(size), output.path, "--out", path.path])
    images.append((size, try Data(contentsOf: path)))
}
var ico = Data()
append16(0, to: &ico)
append16(1, to: &ico)
append16(images.count, to: &ico)
var offset = 6 + images.count * 16
for (size, data) in images {
    ico.append(UInt8(size == 256 ? 0 : size))
    ico.append(UInt8(size == 256 ? 0 : size))
    ico.append(0)
    ico.append(0)
    append16(1, to: &ico)
    append16(32, to: &ico)
    append32(data.count, to: &ico)
    append32(offset, to: &ico)
    offset += data.count
}
for (_, data) in images { ico.append(data) }
try ico.write(to: assets.appendingPathComponent("client-icon.ico"))
print("Generated padded PNG, ICNS, and ICO for the Qt client")
