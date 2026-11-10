# Third-party assets and their licences

Every downloaded file is pinned in `assets/ASSETS.lock` (path, sha256, source URL at a fixed commit); `tools/assets_lock.py`
checks them at configure time and on every build. Only OFL, MIT, CC0 or Apache-2.0 sources with a licence file are used.

| Files | What | Source | Commit / date | Licence |
|---|---|---|---|---|
| `assets/fonts/Baloo2-wght.ttf`, `Baloo2-OFL.txt` | Baloo 2, variable (wght 400-800) | github.com/google/fonts `ofl/baloo2/Baloo2[wght].ttf` | `c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1`, 2026-03-03 | SIL Open Font License 1.1 (`Baloo2-OFL.txt`) |
| `assets/fonts/Nunito-wght.ttf`, `Nunito-OFL.txt` | Nunito, variable (wght 200-1000) | github.com/google/fonts `ofl/nunito/Nunito[wght].ttf` | `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`, 2026-03-12 | SIL Open Font License 1.1 (`Nunito-OFL.txt`) |

Fredoka (OFL), the UI font before UI-1, was removed with it. Already in the repo before this list (not downloaded under these rules): `third_party/` SDL3 (zlib), cJSON (MIT), HandmadeMath (public domain / CC0), **mongoose (GPL-2.0-only or commercial: an open item, see the UI-1 report)**; `assets/cacert.pem` (the Mozilla CA list, MPL-2.0).
