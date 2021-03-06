/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "precompiled.h"
#include "FontFaceLayer.h"
#include <Rocket/Core/Core.h>
#include <Rocket/Core/FontFaceHandle.h>

namespace Rocket {
namespace Core {

FontFaceLayer::FontFaceLayer() : colour(255, 255, 255)
{
	handle = NULL;
	effect = NULL;
	base = NULL;
	deep_copy = 0;
}

FontFaceLayer::~FontFaceLayer()
{
	if (effect != NULL)
		effect->RemoveReference();
}

// Generates the character and texture data for the layer.
bool FontFaceLayer::Initialise(const FontFaceHandle* _handle, FontEffect* _effect, const FontFaceLayer* clone, bool deep_clone)
{
	handle = _handle;
	effect = _effect;
	base = clone;
	deep_copy = deep_clone;

	if (effect != NULL)
	{
		effect->AddReference();
		colour = effect->GetColour();
	}

	const FontGlyphMap& glyphs = handle->GetGlyphs();

	// Clone the geometry and textures from the clone layer.
	if (clone != NULL)
	{
		// Copy the cloned layer's characters.
		MergeFromBase();
	}
	else
	{
		return AddNewGlyphs();
	}


	return true;
}

void FontFaceLayer::MergeFromBase()
{
	characters = base->characters;

	const FontGlyphMap& glyphs = handle->GetGlyphs();

	// Copy (and reference) the cloned layer's textures.
	for (size_t i = textures.size(); i < base->textures.size(); ++i)
		textures.push_back(base->textures[i]);

	// Request the effect (if we have one) adjust the origins as appropriate.
	if (!deep_copy &&
		effect != NULL)
	{
		for (FontGlyphMap::const_iterator i = glyphs.begin(); i != glyphs.end(); ++i)
		{
			const FontGlyph& glyph = i->second;

			CharacterMap::iterator character_iterator = characters.find(i->first);
			if (character_iterator == characters.end())
				continue;

			Character& character = character_iterator->second;

			Vector2i glyph_origin(Math::RealToInteger(character.origin.x), Math::RealToInteger(character.origin.y));
			Vector2i glyph_dimensions(Math::RealToInteger(character.dimensions.x), Math::RealToInteger(character.dimensions.y));

			if (effect->GetGlyphMetrics(glyph_origin, glyph_dimensions, glyph))
			{
				character.origin.x = (float) glyph_origin.x;
				character.origin.y = (float) glyph_origin.y;
			}
			else
				characters.erase(character_iterator);
		}
	}
}

bool FontFaceLayer::AddNewGlyphs()
{
	const FontGlyphMap& glyphs = handle->GetGlyphs();
	texture_layouts.push_back(std::move(std::unique_ptr<TextureLayout>(new TextureLayout())));
	TextureLayout& texture_layout = *texture_layouts.back();
	// Initialise the texture layout for the glyphs.
	for (FontGlyphMap::const_iterator i = glyphs.begin(); i != glyphs.end(); ++i)
	{
		if (characters.find(i->first) != characters.end())
			continue;

		const FontGlyph& glyph = i->second;

		Vector2i glyph_origin(0, 0);
		Vector2i glyph_dimensions = glyph.bitmap_dimensions;

		// Adjust glyph origin / dimensions for the font effect.
		if (effect != NULL)
		{
			if (!effect->GetGlyphMetrics(glyph_origin, glyph_dimensions, glyph))
				continue;
		}

		Character character;
		character.origin = Vector2f((float) (glyph_origin.x + glyph.bearing.x), (float) (glyph_origin.y - glyph.bearing.y));
		character.dimensions = Vector2f((float) glyph_dimensions.x - glyph_origin.x, (float) glyph_dimensions.y - glyph_origin.y);
		characters[i->first] = character;

		// Add the character's dimensions into the texture layout engine.
		texture_layout.AddRectangle(i->first, glyph_dimensions - glyph_origin);
	}

	// Generate the texture layout; this will position the glyph rectangles efficiently and
	// allocate the texture data ready for writing.
#ifdef ROCKET_8BPP_FONTS
    if (!texture_layout.GenerateLayout(1024, 1))
#else
	if (!texture_layout.GenerateLayout(1024, 4))
#endif
		return false;

	// Iterate over each rectangle in the layout, copying the glyph data into the rectangle as
	// appropriate and generating geometry.
	for (int i = 0; i < texture_layout.GetNumRectangles(); ++i)
	{
		TextureLayoutRectangle& rectangle = texture_layout.GetRectangle(i);
		const TextureLayoutTexture& texture = texture_layout.GetTexture(rectangle.GetTextureIndex());
		Character& character = characters[(word) rectangle.GetId()];

		// Set the character's texture index.
		character.texture_index = textures.size() + rectangle.GetTextureIndex();
		character.local_texture_index = rectangle.GetTextureIndex();

		// Generate the character's texture coordinates.
		character.texcoords[0].x = float(rectangle.GetPosition().x) / float(texture.GetDimensions().x);
		character.texcoords[0].y = float(rectangle.GetPosition().y) / float(texture.GetDimensions().y);
		character.texcoords[1].x = float(rectangle.GetPosition().x + rectangle.GetDimensions().x) / float(texture.GetDimensions().x);
		character.texcoords[1].y = float(rectangle.GetPosition().y + rectangle.GetDimensions().y) / float(texture.GetDimensions().y);
	}


	// Generate the textures.
	for (int i = 0; i < texture_layout.GetNumTextures(); ++i)
	{
		std::shared_ptr<Texture> texture(new Texture());
		if (!texture->Load(String(64, "?font::%p/%p/%d/%d", handle, effect, texture_layouts.size() - 1, i)))
			return false;

		textures.push_back(texture);
	}

	return true;
}


// Generates the texture data for a layer (for the texture database).
bool FontFaceLayer::GenerateTexture(const byte*& texture_data, Vector2i& texture_dimensions, int &texture_samples, int layout_id, int texture_id)
{
	TextureLayout& texture_layout = *texture_layouts[layout_id];
	if (texture_id < 0 ||
		texture_id > texture_layout.GetNumTextures())
		return false;

	const FontGlyphMap& glyphs = handle->GetGlyphs();

	// Generate the texture data.
	texture_data = texture_layout.GetTexture(texture_id).AllocateTexture();
	texture_dimensions = texture_layout.GetTexture(texture_id).GetDimensions();
#ifdef ROCKET_8BPP_FONTS
	texture_samples = 1;
#else
	texture_samples = 4;
#endif

	for (int i = 0; i < texture_layout.GetNumRectangles(); ++i)
	{
		TextureLayoutRectangle& rectangle = texture_layout.GetRectangle(i);
		Character& character = characters[(word) rectangle.GetId()];

		if (character.local_texture_index != texture_id)
			continue;

		const FontGlyph& glyph = glyphs.find((word) rectangle.GetId())->second;

		if (effect == NULL)
		{
			// Copy the glyph's bitmap data into its allocated texture.
			if (glyph.bitmap_data != NULL)
			{
				byte* destination = rectangle.GetTextureData();
				byte* source = glyph.bitmap_data;

				for (int j = 0; j < glyph.bitmap_dimensions.y; ++j)
				{
#ifdef ROCKET_8BPP_FONTS
					memcpy(destination, source, glyph.bitmap_dimensions.x);
#else
					for (int k = 0; k < glyph.bitmap_dimensions.x; ++k)
						destination[k * 4 + 3] = source[k];
#endif
					destination += rectangle.GetTextureStride();
					source += glyph.bitmap_dimensions.x;
				}
			}
		}
		else
		{
			effect->GenerateGlyphTexture(rectangle.GetTextureData(), Vector2i(Math::RealToInteger(character.dimensions.x), Math::RealToInteger(character.dimensions.y)), rectangle.GetTextureStride(), glyph);
		}
	}

	return true;
}

// Returns the effect used to generate the layer.
const FontEffect* FontFaceLayer::GetFontEffect() const
{
	return effect;
}

// Returns on the layer's textures.
const Texture* FontFaceLayer::GetTexture(int index)
{
	ROCKET_ASSERT(index >= 0);
	ROCKET_ASSERT(index < GetNumTextures());

	return textures[index].get();
}

// Returns the number of textures employed by this layer.
int FontFaceLayer::GetNumTextures() const
{
	return (int) textures.size();
}

// Returns the layer's colour.
const Colourb& FontFaceLayer::GetColour() const
{
	return colour;
}

}
}
