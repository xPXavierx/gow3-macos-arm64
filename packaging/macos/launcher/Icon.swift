// SPDX-License-Identifier: GPL-2.0-or-later
// Build-time icon conversion only: retain the supplied artwork inside an inset
// rounded square, with transparent corners and a restrained macOS-style shadow.
import Foundation
import CoreGraphics
import ImageIO

let args = CommandLine.arguments
guard args.count == 3,
      let source = CGImageSourceCreateWithURL(URL(fileURLWithPath:args[1]) as CFURL,nil),
      let image = CGImageSourceCreateImageAtIndex(source,0,nil), image.width == image.height else {
    fputs("Expected a square source image and an output directory\n",stderr)
    exit(1)
}
let space = CGColorSpace(name:CGColorSpace.sRGB)!
func canvas(_ size:Int) -> CGContext {
    CGContext(data:nil,width:size,height:size,bitsPerComponent:8,bytesPerRow:size*4,
              space:space,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
}
let context = canvas(1024)
context.setShouldAntialias(true)
let body = CGRect(x:96,y:96,width:832,height:832)
let shape = CGPath(roundedRect:body,cornerWidth:187,cornerHeight:187,transform:nil)
context.saveGState()
context.setShadow(offset:CGSize(width:0,height:-10),blur:18,color:CGColor(gray:0,alpha:0.30))
context.addPath(shape); context.setFillColor(CGColor(gray:0,alpha:1)); context.fillPath()
context.restoreGState()
context.saveGState()
context.addPath(shape); context.clip()
context.interpolationQuality = .high
context.draw(image,in:body)
context.restoreGState()
let master = context.makeImage()!
for size in [16,32,64,128,256,512,1024] {
    let scaled = canvas(size)
    scaled.interpolationQuality = .high
    scaled.draw(master,in:CGRect(x:0,y:0,width:size,height:size))
    let url = URL(fileURLWithPath:args[2]).appendingPathComponent("\(size).png")
    guard let destination = CGImageDestinationCreateWithURL(url as CFURL,"public.png" as CFString,1,nil) else { exit(2) }
    CGImageDestinationAddImage(destination,scaled.makeImage()!,nil)
    guard CGImageDestinationFinalize(destination) else { exit(3) }
}
