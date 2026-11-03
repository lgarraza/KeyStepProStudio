// Chain editing on a Scene's per-track pattern chain (the list a track plays in order, up to 16 patterns, repeats allowed).
// Pure std, no JUCE. Every function returns the new chain; a chain of one pattern is the same as no chain, so a result with
// fewer than 2 entries is returned empty and the caller falls back to the scene's single pattern.
#pragma once
#include "Scene.h"
#include "ProjectMap.h"
#include <algorithm>
#include <vector>

namespace ksp::model::chainedit
{
using Chain = std::vector<int>;

/** A chain of fewer than 2 patterns is no chain. */
inline Chain normalise (Chain c)
{
    if (c.size() < 2)
        c.clear();
    return c;
}

/** Adds pattern `k` (1-16) at the end. With no chain yet, the chain starts from the track's current pattern `base`. Full at 16: unchanged. */
inline Chain append (Chain c, int base, int k)
{
    if (c.empty())
        c.push_back (base);
    if (static_cast<int> (c.size()) < sp::chainLength && k >= 1 && k <= kPatterns)
        c.push_back (k);
    return c;
}

inline Chain removeAt (Chain c, int i)
{
    if (i >= 0 && i < static_cast<int> (c.size()))
        c.erase (c.begin() + i);
    return normalise (std::move (c));
}

inline Chain duplicateAt (Chain c, int i)
{
    if (i >= 0 && i < static_cast<int> (c.size()) && static_cast<int> (c.size()) < sp::chainLength)
        c.insert (c.begin() + i, c[static_cast<std::size_t> (i)]);
    return c;
}

inline Chain setAt (Chain c, int i, int k)
{
    if (i >= 0 && i < static_cast<int> (c.size()) && k >= 1 && k <= kPatterns)
        c[static_cast<std::size_t> (i)] = k;
    return c;
}

/** Moves entry i by `delta` (-1 earlier, +1 later) when it stays inside the chain. */
inline Chain moveAt (Chain c, int i, int delta)
{
    const int j = i + delta;
    if (i >= 0 && j >= 0 && i < static_cast<int> (c.size()) && j < static_cast<int> (c.size()))
        std::swap (c[static_cast<std::size_t> (i)], c[static_cast<std::size_t> (j)]);
    return c;
}
} // namespace ksp::model::chainedit
