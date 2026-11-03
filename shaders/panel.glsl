// The panel draw's block, shared by panel.vert and panel.frag.

#include "common.glsl"

struct PanelDraw {
  vec4 model0;  // rows of the model matrix: the unit quad (-0.5..0.5) to local space
  vec4 model1;
  vec4 model2;
  vec4 uv;      // the texture's columns u0, u1 and rows v0, v1 (v = 0 at the top) it shows
  vec4 arc;     // curved: angle at the left and right edge, height at the top and bottom
  vec4 hover;   // the hovered button in layout pixels: x0, y0, x1, y1
  vec4 shape;   // x radius of the cylinder, y texture pixels per layout pixel,
                // z the hover's corner radius, w glass: the panel's lowest coverage
  float opacity;  // the colour (premultiplied) is scaled by it: fading out
  float unused;
  uint flags;   // below
  float lodBias;  // added to the mip level chosen for the panel's texture
  vec4 hoverClip;  // the hover is cut off outside this, as the list it is in (layout pixels)
};
DECLARE_PUSH(PanelDraw)

// PanelFlag in scene_renderer.h.
const uint kCurved = 1u;       // bent around a cylinder's axis (the model's Y axis), 65 columns
const uint kHover = 2u;
const uint kHoverDanger = 4u;  // a destructive action (quit, delete): highlighted red
const uint kGlass = 16u;       // the scene blurred behind its translucent parts (backdrop)
const uint kPassthrough = 32u; // the room shows where the eye image is key green
// Bits 8..31: the hover rim's colour, sRGB red, green, blue (the menu's accent).
