// SPDX-License-Identifier: GPL-2.0-or-later
// The player app contains verification only. It never contains a signing key.
import Foundation
import CryptoKit

enum TrustError: Error, CustomStringConvertible {
    case invalid(String)
    var description: String { if case .invalid(let reason) = self { return reason }; return "Invalid catalogue" }
}
struct CatalogueManifest: Codable {
    let format: Int
    let title_id: String
    let tested_version: String
    let source_eboot_sha256: String
    let abi: String
    let revision: Int
    let sha256: String
    let bytes: Int
    let issued_at: Int
    let expires_at: Int
}
func verifyManifest(key: Data, manifest: Data, signature: Data, abi: String,
                    now: Int = Int(Date().timeIntervalSince1970)) throws -> CatalogueManifest {
    guard key.count == 32, signature.count == 64, manifest.count <= 8192 else { throw TrustError.invalid("Catalogue bounds") }
    let publicKey = try Curve25519.Signing.PublicKey(rawRepresentation:key)
    guard publicKey.isValidSignature(signature, for:manifest) else { throw TrustError.invalid("Catalogue signature rejected") }
    let m = try JSONDecoder().decode(CatalogueManifest.self, from:manifest)
    guard m.format == 1, m.title_id == "CUSA01623", m.tested_version == "1.02",
          m.source_eboot_sha256 == "d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299",
          m.abi == abi, m.revision > 0, m.revision < 1_000_000_000,
          m.issued_at > 0, m.issued_at <= now+3600, m.expires_at > now, m.expires_at > m.issued_at,
          m.expires_at-m.issued_at <= 366*86400,
          m.bytes > 0, m.bytes <= 16*1024*1024, m.sha256.count == 64,
          m.sha256.allSatisfy({ "0123456789abcdef".contains($0) })
    else { throw TrustError.invalid("Catalogue identity, freshness or checksum rejected") }
    return m
}
func verify(key: Data, manifest: Data, signature: Data, catalogue: Data, abi: String,
            now: Int = Int(Date().timeIntervalSince1970)) throws -> CatalogueManifest {
    let m=try verifyManifest(key:key,manifest:manifest,signature:signature,abi:abi,now:now)
    guard m.bytes == catalogue.count,
          m.sha256 == SHA256.hash(data:catalogue).map({ String(format:"%02x",$0) }).joined()
    else { throw TrustError.invalid("Catalogue body checksum rejected") }
    return m
}
func boundedFile(_ name: String, _ limit: Int) throws -> Data {
    let file=try FileHandle(forReadingFrom:URL(fileURLWithPath:name)); defer { try? file.close() }
    let data=try file.read(upToCount:limit+1) ?? Data()
    guard data.count <= limit else { throw TrustError.invalid("Catalogue file bounds") }
    return data
}

final class BoundedFetch: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    let limit: Int
    var data = Data()
    var problem: Error?
    let finished = DispatchSemaphore(value:0)
    init(limit: Int) { self.limit=limit }
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        problem=TrustError.invalid("Catalogue redirects are not allowed"); completionHandler(nil)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse, completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let http=response as? HTTPURLResponse, http.statusCode == 200,
              response.expectedContentLength <= limit else {
            problem=TrustError.invalid("Catalogue download unavailable or oversized")
            completionHandler(.cancel); return
        }
        completionHandler(.allow)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive chunk: Data) {
        if data.count+chunk.count > limit {
            problem=TrustError.invalid("Catalogue download limit exceeded"); dataTask.cancel()
        } else { data.append(chunk) }
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if problem == nil { problem=error }; finished.signal()
    }
    static func allowed(_ url: URL) -> Bool {
        url.scheme == "https" && url.host == "raw.githubusercontent.com" && url.port == nil &&
        url.user == nil && url.password == nil && url.query == nil && url.fragment == nil &&
        url.path.hasPrefix("/xPXavierx/gow3-macos-arm64/community-catalog/v1/") &&
        !url.absoluteString.contains("%") && !url.path.contains("..")
    }
    func download(_ url: URL) throws -> Data {
        guard Self.allowed(url), limit > 0, limit <= 16*1024*1024 else { throw TrustError.invalid("Catalogue URL rejected") }
        let config=URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest=10; config.timeoutIntervalForResource=20
        config.httpCookieStorage=nil; config.urlCache=nil
        let session=URLSession(configuration:config,delegate:self,delegateQueue:nil)
        defer { session.invalidateAndCancel() }
        var request=URLRequest(url:url,cachePolicy:.reloadIgnoringLocalCacheData)
        request.setValue("GOW3-ARM64-Catalogue/1",forHTTPHeaderField:"User-Agent")
        session.dataTask(with:request).resume()
        guard finished.wait(timeout:.now()+25) == .success else { throw TrustError.invalid("Catalogue download timed out") }
        if let error=problem { throw error }; return data
    }
}

func selfTest() throws {
    let privateKey=Curve25519.Signing.PrivateKey()
    let data=Data("numeric catalogue fixture".utf8), now=Int(Date().timeIntervalSince1970)
    let m=CatalogueManifest(format:1,title_id:"CUSA01623",tested_version:"1.02",
        source_eboot_sha256:"d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299",
        abi:"f9b9a649560a0b85",revision:1,sha256:SHA256.hash(data:data).map {String(format:"%02x",$0)}.joined(),
        bytes:data.count,issued_at:now,expires_at:now+86400)
    let encoded=try JSONEncoder().encode(m), signature=try privateKey.signature(for:encoded)
    _ = try verify(key:privateKey.publicKey.rawRepresentation,manifest:encoded,signature:signature,catalogue:data,abi:m.abi)
    func rejects(_ operation: () throws -> Void) { do { try operation(); fatalError("Invalid catalogue accepted") } catch {} }
    rejects { _ = try verify(key:privateKey.publicKey.rawRepresentation,manifest:encoded,signature:Data(repeating:0,count:64),catalogue:data,abi:m.abi) }
    rejects { _ = try verify(key:Curve25519.Signing.PrivateKey().publicKey.rawRepresentation,manifest:encoded,signature:signature,catalogue:data,abi:m.abi) }
    rejects { _ = try verify(key:privateKey.publicKey.rawRepresentation,manifest:encoded,signature:signature,catalogue:Data("tampered".utf8),abi:m.abi) }
    rejects { _ = try verify(key:privateKey.publicKey.rawRepresentation,manifest:encoded,signature:signature,catalogue:data,abi:"0000000000000000") }
    rejects { _ = try verify(key:privateKey.publicKey.rawRepresentation,manifest:encoded,signature:signature,catalogue:data,abi:m.abi,now:now+2*86400) }
    precondition(!BoundedFetch.allowed(URL(string:"https://evil.invalid/catalogue")!))
    precondition(!BoundedFetch.allowed(URL(string:"http://raw.githubusercontent.com/xPXavierx/gow3-macos-arm64/community-catalog/v1/a")!))
    precondition(!BoundedFetch.allowed(URL(string:"https://raw.githubusercontent.com/xPXavierx/gow3-macos-arm64/community-catalog/v1/a?url=evil")!))
    print("{\"signature_tampering_expiry_abi_origin_checks\":true}")
}
do {
    let a=CommandLine.arguments
    if a.count == 2 && a[1] == "--self-test" { try selfTest() }
    else if a.count == 7 && a[1] == "verify" {
        let m=try verify(key:boundedFile(a[2],32),manifest:boundedFile(a[3],8192),
            signature:boundedFile(a[4],64),catalogue:boundedFile(a[5],16*1024*1024),abi:a[6])
        print(String(data:try JSONEncoder().encode(m),encoding:.utf8)!)
    } else if a.count == 6 && a[1] == "manifest" {
        let m=try verifyManifest(key:boundedFile(a[2],32),manifest:boundedFile(a[3],8192),
                                 signature:boundedFile(a[4],64),abi:a[5])
        print(String(data:try JSONEncoder().encode(m),encoding:.utf8)!)
    } else if a.count == 5 && a[1] == "download", let url=URL(string:a[2]), let limit=Int(a[3]) {
        let data=try BoundedFetch(limit:limit).download(url)
        try data.write(to:URL(fileURLWithPath:a[4]),options:.atomic)
    } else { throw TrustError.invalid("Invalid catalogue verifier arguments") }
} catch {
    fputs("Catalogue trust: \(error)\n",stderr); exit(1)
}
