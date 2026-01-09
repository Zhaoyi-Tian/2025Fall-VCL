#version 330 core

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D uMSDFTexture;
uniform float uPxRange;  // Pixel range used during MSDF generation

// Median function for MSDF
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample the MSDF texture
    vec3 msd = texture(uMSDFTexture, TexCoord).rgb;

    // Calculate the signed distance using the median of the three channels
    float sd = median(msd.r, msd.g, msd.b);

    // Calculate screen-space distance
    // The distance value is in the range [0, 1], where 0.5 is the edge
    // We need to scale by the pixel range to get proper anti-aliasing
    float screenPxDistance = uPxRange * (sd - 0.5);

    // Calculate opacity with smooth anti-aliasing
    // Add 0.5 to center the edge, then clamp to [0, 1]
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    // Apply the final color with calculated opacity
    FragColor = vec4(Color.rgb, Color.a * opacity);

    // Discard fully transparent fragments for performance
    if (FragColor.a < 0.01) {
        discard;
    }
}
