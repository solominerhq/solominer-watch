# Branded phone setup

The watch hosts this page locally over its setup Wi-Fi. It needs no internet,
account, external fonts or image downloads. Editable files are in
`assets/setup-portal/`; the logo is embedded SVG and the stars are CSS.

The page presents Wi-Fi, a Bitcoin receiving address and the clock. Pool and
worker settings remain under Advanced. Firmware upload is accessible from the
footer and `/update`. The upload uses WiFiManager's existing `/u` handler.

Saved timezones are preserved, including saved custom POSIX rules. A phone's
timezone is only suggested when the watch has no saved timezone. Leaving the
Wi-Fi password blank keeps the saved password only for the same saved network;
the API never sends that password to the phone. New networks require a password
unless explicitly marked open.

`src/network/setup_portal.cpp` adapts the pinned WiFiManager 2.0.17 routes.
Network discovery is asynchronous. Settings submissions require POST and a
random session token, validate fields on the watch, and use the existing atomic
configuration save callback. A failed configuration write keeps the prior
settings and does not queue a Wi-Fi connection. The success page is shown only
after the watch acknowledges that save. Wi-Fi credential storage, connection
retries, portal shutdown and firmware-upload handling remain WiFiManager-owned.

## Editing and validation

After changing HTML, CSS or JavaScript, regenerate the flash-resident asset:

```sh
python3 scripts/build_setup_portal.py
python3 -m unittest discover -s tests -p 'test_setup_portal*.py'
pio run -e Waveshare-AMOLED-206
```

The generated header is committed so normal firmware builds need no additional
frontend tooling. The asset test detects stale generated content. Source exports
include the editable assets and generator.

Before shipping, check on a physical watch and phone:

1. Open setup from the watch, join its Wi-Fi and load the captive page.
2. Scan networks, enter a protected network and save a valid receiving address.
   Verify settings survive restart and the watch connects.
3. Reopen setup; confirm the timezone and address remain. Save without entering
   the existing network password and verify reconnection.
4. Enter a different network, including a same-phone hotspot, and confirm the
   password is required. Turn the hotspot back on after leaving setup Wi-Fi.
5. Check invalid entries and a wrong Wi-Fi password leave a usable recovery path.
6. Test the firmware-update file picker in a normal browser. Use an application
   `.bin`, keep the watch powered, and confirm a successful restart.

Browser fixtures and host tests cannot verify the actual radio, captive-browser
behavior or flash writes on the physical watch.
