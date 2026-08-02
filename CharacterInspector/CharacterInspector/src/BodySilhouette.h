#pragma once

#include <string>

namespace BodySilhouette
{
    // Register mod gui/silhouette with Ogre (idempotent).
    bool EnsureResources();

    // Load body_hitmap.png into memory for pixel picking (idempotent).
    bool EnsureHitmap();

    // Map Kenshi anatomy part name -> layer id (head, chest, ...). Empty if unknown.
    std::string LayerIdForPartName(const std::string& partName);

    // Sample hitmap at normalized UV (0..1). Empty string if background / unknown.
    std::string SampleLayerAt(float u, float v);
}
