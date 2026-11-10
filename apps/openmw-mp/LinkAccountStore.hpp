#ifndef ARENAMP_LINKACCOUNTSTORE_HPP
#define ARENAMP_LINKACCOUNTSTORE_HPP
#include "LinkServer.hpp"
namespace mwmp
{
    // JSON accounts only. Reads existing profiles; never rewrites their passwords.
    LinkCallbacks jsonLinkCallbacks(const std::string& dataDirectory);
}
#endif
