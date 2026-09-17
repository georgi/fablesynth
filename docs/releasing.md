# Releasing FableSynth

Pushing a `v*` tag builds and publishes the four plug-ins for macOS, Windows,
and Linux. A manual run builds the same downloadable workflow artifacts without
creating a GitHub release.

## macOS signing and notarization

The release workflow requires an Apple Developer Program membership and these
GitHub Actions repository secrets:

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE` | Base64-encoded Developer ID Application `.p12` certificate and private key |
| `MACOS_CERTIFICATE_PASSWORD` | Password used when exporting the `.p12` |
| `APPLE_API_KEY` | Contents of an App Store Connect API `.p8` private key |
| `APPLE_API_KEY_ID` | App Store Connect API key ID |
| `APPLE_API_ISSUER_ID` | App Store Connect API issuer ID |

For example, encode the certificate on macOS with:

```sh
base64 -i DeveloperIDApplication.p12 | pbcopy
```

The API key must have permission to use Apple's notary service. Store the full
`.p8` contents, including its `BEGIN PRIVATE KEY` and `END PRIVATE KEY` lines.

The macOS job fails before packaging if these credentials are absent or the
certificate is not a Developer ID Application certificate. This prevents a tag
from publishing another ad-hoc-signed macOS release that Gatekeeper blocks.

The workflow signs every VST3, AU, and standalone bundle with the hardened
runtime and a secure timestamp. It submits each distributed ZIP to Apple's
notary service, staples the resulting tickets to the enclosed bundles, validates
them, and then recreates the release ZIPs from the stapled bundles.
