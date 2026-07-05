#pragma once

#include "HagUIAPI.h"

namespace hag {

// Fills the HagUI page that the loader created for HagGeneral.
class HagUIBridge {
public:
    static void Register(HagUI_PageHandle* page);
};

}  // namespace hag
