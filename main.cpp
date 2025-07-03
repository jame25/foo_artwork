#include "stdafx.h"

// Declaration of component version information
DECLARE_COMPONENT_VERSION("Artwork Display Component","1.0","Advanced artwork display component with online API fallback support");

// Prevent users from renaming the component
VALIDATE_COMPONENT_FILENAME("foo_artwork.dll");

// Activate cfg_var downgrade functionality if enabled
FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;