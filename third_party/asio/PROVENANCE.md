# Steinberg ASIO SDK provenance

Version: ASIO SDK 2.3.4 (2025-10-15).
Official licensing entry: https://www.steinberg.net/developers/asiosdk-open/
Official download entry: https://www.steinberg.net/asiosdk
Resolved official archive: https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
Archive bytes: 8910208
Archive SHA-256: d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
Retrieved: 2026-10-24.

Selected license: GNU General Public License version 3 (the alternative expressly offered by the SDK LICENSE.txt). No proprietary Steinberg agreement was accepted. No ASIO logo or endorsement is claimed. ASIO is a trademark of Steinberg Media Technologies GmbH.

The three files in include/ are unchanged official common/asio.h, common/asiosys.h and common/iasiodrv.h. LICENSE.txt is unchanged from the archive root. The application implements its own host against these official interfaces; it does not redefine the driver ABI and does not use a third-party application framework. On non-MSVC Windows compilers the include wrapper applies the SDK's documented 4-byte structure packing.

The downloaded zip and expanded sdk/ directory are retrieval/build-work inputs, not required to build. Distribute include/, LICENSE.txt and this provenance with the project and GPLv3 license/source obligations. No third-party hardware driver is included or installed.

Header SHA-256 (unchanged official files):
- asio.h: 93d2f80d49b4ac9c963514e7c1307ab5111d8104246192233745beb058534ea1
- asiosys.h: f44a1ed0fd7c056ff047aeebc4c05a8c185f0585a24d8debc55c70107777dbef
- iasiodrv.h: 86ad4788452c74823653cf57d4bd557c48bb6542d7e6eb7fff80102c705a066d
