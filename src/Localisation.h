// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Localisation
{
    /** Installs a translation for the system language (if we have one) and picks
        a UI font that can render it. Call once at startup, after the message
        manager exists. Strings in the code are English and wrapped in TRANS(). */
    void install();
}
