/*
	This file is part of Warzone 2100.
	Copyright (C) 2024  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** \file
 *  Info Button
 */

#pragma once

#include "lib/widget/widget.h"
#include "lib/widget/button.h"

class WzInfoButton : public W_BUTTON
{
protected:
	WzInfoButton() {}
public:
	static std::shared_ptr<WzInfoButton> make();
	void setImageDimensions(int imageSize);
	int32_t idealHeight() override;
protected:
	void display(int xOffset, int yOffset) override;
private:
	int imageDimensions = 16;
	PIELIGHT imageColor = WZCOL_TEXT_MEDIUM;
	PIELIGHT imageColorHighlighted = pal_RGBA(255,255,255,255);
};
