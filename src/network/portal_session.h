#pragma once
#include "portal_cleanup.h"

namespace network {
// Process and stop WiFiManager on its owning task. Display input only queues
// an exit request; an HTTP save/firmware upload finishes before it is handled.
template <typename Manager, typename TakeExit, typename Idle>
bool runProvisioningPortal(Manager& manager, TakeExit takeExit, Idle idle) {
    while (manager.getConfigPortalActive()) {
        if (takeExit()) {
            stopProvisioningPortals(manager);
            return true;
        }
        if (manager.process()) break;
        idle();
    }
    // A request may arrive during a successful save or portal shutdown.
    return takeExit();
}
}
