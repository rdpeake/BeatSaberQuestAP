#pragma once

#include <string>

namespace Archipelago {

    // collects the device's trusted root certificates into one PEM file for mbedTLS;
    // returns the written path, or an empty string when no certificates were found
    std::string WriteSystemCertificateBundle(std::string const& bundlePath);
}
