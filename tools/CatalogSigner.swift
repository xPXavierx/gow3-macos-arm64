// SPDX-License-Identifier: GPL-2.0-or-later
// Maintainer-only tool. Never bundle this executable in the player application.
import Foundation
import Security
import CryptoKit
let service="org.gow3pc.community-catalogue.signing.v1"
func key(create: Bool) throws -> Curve25519.Signing.PrivateKey {
    let query:[String:Any]=[kSecClass as String:kSecClassGenericPassword,
        kSecAttrService as String:service,kSecAttrAccount as String:"official",
        kSecReturnData as String:true,kSecMatchLimit as String:kSecMatchLimitOne]
    var result:CFTypeRef?
    let status=SecItemCopyMatching(query as CFDictionary,&result)
    if status == errSecSuccess, let data=result as? Data { return try .init(rawRepresentation:data) }
    guard status == errSecItemNotFound && create else { throw NSError(domain:NSOSStatusErrorDomain,code:Int(status)) }
    let generated=Curve25519.Signing.PrivateKey()
    let record:[String:Any]=[kSecClass as String:kSecClassGenericPassword,
        kSecAttrService as String:service,kSecAttrAccount as String:"official",
        kSecAttrAccessible as String:kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        kSecValueData as String:generated.rawRepresentation]
    let added=SecItemAdd(record as CFDictionary,nil)
    guard added == errSecSuccess else { throw NSError(domain:NSOSStatusErrorDomain,code:Int(added)) }
    return generated
}
do {
    let a=CommandLine.arguments
    if a.count == 3 && a[1] == "init" {
        try key(create:true).publicKey.rawRepresentation.write(to:URL(fileURLWithPath:a[2]),options:.atomic)
        print("Public catalogue key saved; private key stays in macOS Keychain")
    } else if a.count == 4 && a[1] == "sign" {
        let data=try Data(contentsOf:URL(fileURLWithPath:a[2]))
        guard data.count <= 8192 else { throw NSError(domain:"Manifest bounds",code:1) }
        try key(create:false).signature(for:data).write(to:URL(fileURLWithPath:a[3]),options:.atomic)
        print("Catalogue manifest signed")
    } else { throw NSError(domain:"Use init PUBLIC_KEY or sign MANIFEST SIGNATURE",code:1) }
} catch { fputs("Catalogue signer: \(error)\n",stderr); exit(1) }
