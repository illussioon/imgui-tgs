#include "tgs/tgs_embed.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace tgs {
namespace {

struct Registry
{
    std::mutex                        mutex;
    std::vector<const EmbeddedAsset*> items; // sorted by name
};

Registry& GetRegistry()
{
    static Registry r;
    return r;
}

bool NameLess(const EmbeddedAsset* a, const char* name) { return std::strcmp(a->name, name) < 0; }

} // namespace

void RegisterEmbedded(const EmbeddedAsset* assets, size_t count)
{
    if (!assets)
        return;
    Registry& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    for (size_t i = 0; i < count; ++i)
    {
        const EmbeddedAsset* a = &assets[i];
        if (!a->name || !a->data || a->size == 0)
            continue;
        auto it = std::lower_bound(r.items.begin(), r.items.end(), a->name, NameLess);
        if (it != r.items.end() && std::strcmp((*it)->name, a->name) == 0)
            continue; // same table again or name collision: first wins
        r.items.insert(it, a);
    }
}

void UnregisterEmbedded(const EmbeddedAsset* assets, size_t count)
{
    if (!assets)
        return;
    Registry& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    const EmbeddedAsset* begin = assets;
    const EmbeddedAsset* end = assets + count;
    r.items.erase(std::remove_if(r.items.begin(), r.items.end(),
                                 [&](const EmbeddedAsset* a) { return a >= begin && a < end; }),
                  r.items.end());
}

const EmbeddedAsset* FindEmbedded(const char* name)
{
    if (!name)
        return nullptr;
    Registry& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = std::lower_bound(r.items.begin(), r.items.end(), name, NameLess);
    if (it != r.items.end() && std::strcmp((*it)->name, name) == 0)
        return *it;
    return nullptr;
}

size_t EmbeddedCount()
{
    Registry& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.items.size();
}

const EmbeddedAsset* EmbeddedAt(size_t index)
{
    Registry& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mutex);
    return index < r.items.size() ? r.items[index] : nullptr;
}

} // namespace tgs
