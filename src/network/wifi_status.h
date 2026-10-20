#pragma once
namespace network {
// Published by Wi-Fi events; independent of the retry timer and pool state.
enum class WifiStage { Waiting, Connecting, GettingAddress, Connected };
enum class WifiProblem { Unknown, NotFound, Credentials };
// Driver reason numbers from the pinned ESP-IDF. Handshake failures can also
// mean poor reception; ask the owner to check rather than declare a bad password.
inline WifiProblem problemForReason(int reason) {
    return reason==201 ? WifiProblem::NotFound :
        (reason==202 || reason==15 || reason==204) ? WifiProblem::Credentials :
        WifiProblem::Unknown;
}
WifiProblem wifiProblem();
inline const char* problemHint(WifiProblem problem) {
    return problem==WifiProblem::NotFound ? "Network not found" :
        problem==WifiProblem::Credentials ? "Check Wi-Fi password" :
        "Check network or hotspot";
}
}
