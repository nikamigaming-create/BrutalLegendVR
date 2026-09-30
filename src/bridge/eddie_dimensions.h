#pragma once

namespace blvr_rig {
// Presentation dimensions in meters. Retail geometry is authored for a
// stylized third-person character: its 29 cm hands and 184 cm guitar are too
// large at tracked-hand distance. Keep the silhouette, with adult arm reach
// and a separate, consistently scaled hand/held-item size.
constexpr float EyeHeightMeters = 1.72f;
constexpr float HandLengthMeters = .21f;
constexpr unsigned GripCalibrationVersion = 2;
}
