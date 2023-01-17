#include <NeoPixelBus.h>

const uint16_t TOTAL_WIDTH = 32;
const uint16_t TOTAL_HEIGHT = 8;

const uint16_t TOTAL_PIXELS = TOTAL_WIDTH * TOTAL_HEIGHT;

// Efficient connection via DMA on pin RDX0 GPIO3 RX
// See <https://github.com/Makuna/NeoPixelBus/wiki/FAQ-%231>
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(TOTAL_PIXELS);

// bitbanging (Fallback)
// const int PIN_MATRIX = 13; // D7
// NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod> strip(TOTAL_PIXELS, PIN_MATRIX);

NeoTopology<ColumnMajorAlternating180Layout> topo(TOTAL_WIDTH, TOTAL_HEIGHT);

struct ColorBufferColor
{
	float r;
	float g;
	float b;
};
struct ColorBufferColor colorBuffer[TOTAL_PIXELS];

float globalBrightness = 0.0;

void matrix_setup(float brightness)
{
	strip.Begin();
	globalBrightness = brightness;
}

void matrix_brightness(float brightness)
{
	globalBrightness = brightness;
}

void matrix_update()
{
	for (uint16_t i = 0; i < TOTAL_PIXELS; i++)
	{
		auto r = colorBuffer[i].r * globalBrightness;
		auto g = colorBuffer[i].g * globalBrightness;
		auto b = colorBuffer[i].b * globalBrightness;
		strip.SetPixelColor(i, RgbColor(r, g, b));
	}

	strip.Show();
}

void matrix_fill(uint8_t red, uint8_t green, uint8_t blue)
{
	struct ColorBufferColor color = {(float)red, (float)green, (float)blue};
	for (uint16_t i = 0; i < TOTAL_PIXELS; i++)
	{
		colorBuffer[i] = color;
	}
}

void matrix_pixel(uint16_t x, uint16_t y, uint8_t red, uint8_t green, uint8_t blue)
{
	auto i = topo.Map(x, y);
	struct ColorBufferColor color = {(float)red, (float)green, (float)blue};
	colorBuffer[i] = color;
}
