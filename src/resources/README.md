# Options button bitmaps

These bitmaps are byte-identical copies of the existing reconstructed player's resources in `ttpres/resources/original/bitmaps`:

| File | Resource ID |
|---|---:|
| options-add.bmp | 1027 |
| options-delete.bmp | 1039 |
| options-up.bmp | 1042 |
| options-down.bmp | 1045 |
| options-edit.bmp | 352 (0x160) |

They retain their original dimensions (the four list controls are 16×15) and RGB(192,192,192) mask color. The media source list uses the same no-caption button painting and dialog-unit dimensions as the player's existing options controls. These local fallback resources keep the standalone DLL usable with compatible hosts; when a colocated `ttpres.dll` is present, its corresponding bitmaps are preferred.
