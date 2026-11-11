// BOOK-ENGINE decision 7: ASTC 4x4 through the OS's own encoder (ImageIO, org.khronos.astc): no third-party binary.
// usage: swift tools/book/astc_encode.swift IN.png OUT.astc [IN.png OUT.astc ...]
// ImageIO writes the bottom row of blocks first (GL order); the image is flipped before encoding so the file's first
// block is the top-left one, which is what Metal and Vulkan read.
import CoreGraphics
import Foundation
import ImageIO

var args = Array(CommandLine.arguments.dropFirst())
if args.isEmpty || args.count % 2 != 0 {
    FileHandle.standardError.write("usage: astc_encode IN.png OUT.astc [...]\n".data(using: .utf8)!)
    exit(2)
}
while !args.isEmpty {
    let inp = args.removeFirst(), out = args.removeFirst()
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: inp) as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
        FileHandle.standardError.write("astc_encode: cannot read \(inp)\n".data(using: .utf8)!)
        exit(1)
    }
    let w = img.width, h = img.height
    let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.translateBy(x: 0, y: CGFloat(h))
    ctx.scaleBy(x: 1, y: -1)
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
    guard let flipped = ctx.makeImage(),
          let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: out) as CFURL, "org.khronos.astc" as CFString, 1, nil) else {
        exit(1)
    }
    CGImageDestinationAddImage(dst, flipped, [kCGImagePropertyASTCBlockSize: 0x44] as CFDictionary)
    if !CGImageDestinationFinalize(dst) {
        FileHandle.standardError.write("astc_encode: ImageIO refused \(inp)\n".data(using: .utf8)!)
        exit(1)
    }
}
