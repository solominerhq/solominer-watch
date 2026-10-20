# Security

Please report suspected vulnerabilities privately to **hello@getsolominer.com**
with the subject **SoloMiner firmware security**. Do not post credentials or a
working exploit in a public issue while it is being investigated.
No response-time guarantee is made.

Include the firmware version, supported board, reproduction steps, impact,
and sanitized logs. Never send a wallet seed phrase or private key.

## Trust boundaries

- The watch is a mining client, not a wallet or Bitcoin full node.
- A Bitcoin receiving address is public information. Wi-Fi passwords are not.
- The setup access point has a documented shared default password. Treat setup
  as a local provisioning interface; close it when finished and do not expose
  the portal to the internet.
- Firmware installation grants code control of the device. Install only builds
  from a source you trust. A SHA-256 manifest detects a mismatch; by itself it
  does not authenticate who produced the firmware.
- Debug output and diagnostics may contain identifying details. Review before sharing.

Firmware releases follow the dependency and hardware checks in
[the release process](docs/OPEN_SOURCE_RELEASE.md).
