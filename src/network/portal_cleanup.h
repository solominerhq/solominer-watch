#pragma once

namespace network {
// WiFiManager 2.0.17 stopConfigPortal() dereferences its HTTP server even
// when no portal was opened. Saved-network boots have no server to stop.
// Query live state so cleanup also remains safe after a portal closes itself.
template <typename Manager>
void stopProvisioningPortals(Manager& manager) {
    if (manager.getWebPortalActive()) manager.stopWebPortal();
    if (manager.getConfigPortalActive()) manager.stopConfigPortal();
}
} // namespace network
