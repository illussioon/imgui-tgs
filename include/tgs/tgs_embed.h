// tgs-parse-imgui — registry of stickers embedded at compile time (see cmake/TgsEmbed.cmake)
#pragma once

#include <cstddef>

namespace tgs {

struct EmbeddedAsset
{
    const char*          name;   // file name without extension (+ PREFIX)
    const unsigned char* data;   // raw .tgs bytes (gzip) as stored in the binary
    size_t               size;
};

// Registers a table of assets. Pointers must stay valid for the module lifetime
// (generated tables are static). Re-registering the same table is a no-op.
// If a name is already taken by another table, the first registration wins.
void RegisterEmbedded(const EmbeddedAsset* assets, size_t count);

// Removes all assets that point into this table (e.g. before unloading a plugin DLL
// that registered stickers into a host-wide registry).
void UnregisterEmbedded(const EmbeddedAsset* assets, size_t count);

const EmbeddedAsset* FindEmbedded(const char* name);
size_t               EmbeddedCount();
const EmbeddedAsset* EmbeddedAt(size_t index);   // sorted by name

struct EmbeddedRegistrar
{
    EmbeddedRegistrar(const EmbeddedAsset* assets, size_t count) { RegisterEmbedded(assets, count); }
};

} // namespace tgs
