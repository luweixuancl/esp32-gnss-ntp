#pragma once
// Preferences is only needed as a member type of SettingsStore; LocalClock
// host tests never construct SettingsStore. Provide an empty shell.
class Preferences {};
