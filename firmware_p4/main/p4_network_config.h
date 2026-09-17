#pragma once

/*
 * Development-only Wi-Fi settings for the P4 handheld.
 *
 * Keep this file as the single place to change router credentials while the
 * firmware networking settings UI does not exist yet. Do not put router IPs
 * here; Home Base is discovered by hostname first and UDP broadcast second.
 */
#define TOP_SPOT_WIFI_SSID "GL-SFT1200-cc0"
#define TOP_SPOT_WIFI_PASSWORD "#Jsx6wf091"

#define TOP_SPOT_HOMEBASE_HOSTNAME "topspot-homebase.local"
#define TOP_SPOT_HOMEBASE_PORT 8000
#define TOP_SPOT_HOMEBASE_DISCOVERY_PORT 37020
