/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

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
/** @file
 *  Functions for the edit box widget.
 */

#include <string.h>

#include "lib/framework/frame.h"
#include "lib/framework/utf.h"
#include "lib/framework/wzapp.h"
#include "widget.h"
#include "widgint.h"
#include "editbox.h"
#include "form.h"
#include "lib/ivis_opengl/pieblitfunc.h"


/* Pixel gap between edge of edit box and text */
#define WEDB_XGAP	4

/* Size of the overwrite cursor */
#define WEDB_CURSORSIZE		8

/* The time the cursor blinks for */
#define WEDB_BLINKRATE		800

/* Number of characters to jump the edit box text when moving the cursor */
#define WEDB_CHARJUMP		6

// Max size for a string in a editbox
#define EB_MAX_STRINGSIZE 72

W_EDBINIT::W_EDBINIT()
	: pText(nullptr)
	, FontID(font_regular)
	, pBoxDisplay(nullptr)
{}

W_EDITBOX::W_EDITBOX(W_EDBINIT const *init)
	: WIDGET(init, WIDG_EDITBOX)
	, state(WEDBS_FIXED)
	, FontID(init->FontID)
	, blinkOffset(wzGetTicks())
	, maxStringSize(EB_MAX_STRINGSIZE)
	, insPos(0)
	, printStart(0)
	, printChars(0)
	, printWidth(0)
	, pBoxDisplay(init->pBoxDisplay)
	, HilightAudioID(WidgGetHilightAudioID())
	, ClickedAudioID(WidgGetClickedAudioID())
	, ErrorAudioID(WidgGetErrorAudioID())
	, AudioCallback(WidgGetAudioCallback())
	, boxColourFirst(WZCOL_FORM_DARK)
	, boxColourSecond(WZCOL_FORM_LIGHT)
	, boxColourBackground(WZCOL_FORM_BACKGROUND)
{
	char const *text = init->pText;
	if (!text)
	{
		text = "";
	}
	aText = WzString::fromUtf8(text);

	initialise();

	ASSERT((init->style & ~(WEDB_PLAIN | WIDG_HIDDEN)) == 0, "Unknown edit box style");
}

W_EDITBOX::W_EDITBOX()
	: WIDGET()
	, state(WEDBS_FIXED)
	, FontID(font_regular)
	, blinkOffset(wzGetTicks())
	, maxStringSize(EB_MAX_STRINGSIZE)
	, insPos(0)
	, printStart(0)
	, printChars(0)
	, printWidth(0)
	, pBoxDisplay(nullptr)
	, HilightAudioID(WidgGetHilightAudioID())
	, ClickedAudioID(WidgGetClickedAudioID())
	, ErrorAudioID(WidgGetErrorAudioID())
	, AudioCallback(WidgGetAudioCallback())
	, boxColourFirst(WZCOL_FORM_DARK)
	, boxColourSecond(WZCOL_FORM_LIGHT)
	, boxColourBackground(WZCOL_FORM_BACKGROUND)
{}

W_EDITBOX::~W_EDITBOX()
{
	/* Note the edit state */
	unsigned editState = state & WEDBS_MASK;

	/* Only have anything to do if the widget is being edited */
	if ((editState & WEDBS_MASK) == WEDBS_FIXED)
	{
		return;
	}

	// If the edit box still somehow has focus, and is editable, need to StopTextInput()
	// (May be able to remove this once more refactoring of the game menus / in-game UI occurs)
	debug(LOG_INFO, "Editbox seems to still have focus, and is editable, as it's being destroyed.");
	StopTextInput(this); // force-stop text input if this EditBox somehow still has the input
}

void W_EDITBOX::initialise()
{
	state = WEDBS_FIXED;
	printStart = 0;
	maxStringSize = EB_MAX_STRINGSIZE;
	fitStringStart();
}


/* Insert a character into a text buffer */
bool W_EDITBOX::insertChar(WzUniCodepoint ch)
{
	if (ch.isNull())
	{
		return false;
	}

	ASSERT(insPos <= aText.length(), "Invalid insertion point");
	if (aText.length() >= maxStringSize)
	{
		if (AudioCallback)
		{
			AudioCallback(ErrorAudioID);
		}
		return false;		// string too big, just return
	}
	/* Move the end of the string up by one (including terminating \0) */
	/* Insert the character */
	aText.insert(insPos, ch);

	/* Update the insertion point */
	++insPos;

	return true;
}


/* Put a character into a text buffer overwriting any text under the cursor */
bool W_EDITBOX::overwriteChar(WzUniCodepoint ch)
{
	if (ch.isNull())
	{
		return false;
	}

	ASSERT(insPos <= aText.length(), "overwriteChar: Invalid insertion point");
	dirty = true;

	if (insPos == aText.length())
	{
		// At end of string.
		return insertChar(ch);
	}

	/* Store the character */
	aText[insPos] = ch;

	/* Update the insertion point */
	++insPos;

	return true;
}


/* Delete a character to the right of the position */
void W_EDITBOX::delCharRight()
{
	ASSERT(insPos <= aText.length(), "Invalid deletion point");

	/* Can't delete if we are at the end of the string */
	/* Move the end of the string down by one */
	aText.remove(insPos, 1);
}


/* Delete a character to the left of the position */
void W_EDITBOX::delCharLeft()
{
	/* Can't delete if we are at the start of the string */
	if (insPos == 0)
	{
		return;
	}

	--insPos;
	delCharRight();
}


void W_EDITBOX::geometryChanged()
{
	/* Note the edit state */
	unsigned editState = state & WEDBS_MASK;

	/* For now, only handle fit recalculation if not being edited */
	if (!((editState & WEDBS_MASK) == WEDBS_FIXED))
	{
		return;
	}
	fitStringStart();
}


/* Calculate how much of the start of a string can fit into the edit box */
void W_EDITBOX::fitStringStart()
{
	// We need to calculate the whole string's pixel size.
	// From QuesoGLC's notes: additional processing like kerning creates strings of text whose dimensions are not directly
	// related to the simple juxtaposition of individual glyph metrics. For example, the advance width of "VA" isn't the
	// sum of the advances of "V" and "A" taken separately.
	WzString tmp = aText;
	tmp.remove(0, printStart);  // Ignore the first printStart characters.

	while (!tmp.isEmpty())
	{
		int pixelWidth = iV_GetTextWidth(tmp, FontID);

		if (pixelWidth <= width() - (WEDB_XGAP * 2 + WEDB_CURSORSIZE))
		{
			printChars = tmp.length();
			printWidth = pixelWidth;
			return;
		}

		tmp.remove(tmp.length() - 1, 1);  // Erase last char.
	}

	printChars = 0;
	printWidth = 0;
}


/* Calculate how much of the end of a string can fit into the edit box */
void W_EDITBOX::fitStringEnd()
{
	WzString tmp = aText;

	printStart = 0;

	while (!tmp.isEmpty())
	{
		int pixelWidth = iV_GetTextWidth(tmp, FontID);

		if (pixelWidth <= width() - (WEDB_XGAP * 2 + WEDB_CURSORSIZE))
		{
			printChars = tmp.length();
			printWidth = pixelWidth;
			return;
		}

		tmp.remove(0, 1);  // Erase first char.
		++printStart;
	}

	printChars = 0;
	printWidth = 0;
}

void W_EDITBOX::setCursorPosPixels(int xPos)
{
	WzString tmp = aText;
	tmp.remove(0, printStart);  // Consider only the visible text.
	tmp.remove(printChars, tmp.length());

	int prevDelta = INT32_MAX;
	int prevPos = printStart + tmp.length();
	while (!tmp.isEmpty())
	{
		int pixelWidth = iV_GetTextWidth(tmp, FontID);
		int delta = pixelWidth - (xPos - (WEDB_XGAP + WEDB_CURSORSIZE / 2));
		int pos = printStart + tmp.length();

		if (delta <= 0)
		{
			insPos = -delta < prevDelta ? pos : prevPos;
			return;
		}

		tmp.remove(tmp.length() - 1, 1);  // Erase last char.

		prevDelta = delta;
		prevPos = pos;
	}

	insPos = printStart;
}


void W_EDITBOX::run(W_CONTEXT *psContext)
{
	/* Note the edit state */
	unsigned editState = state & WEDBS_MASK;

	/* Only have anything to do if the widget is being edited */
	if ((editState & WEDBS_MASK) == WEDBS_FIXED)
	{
		return;
	}
	dirty = true;
	StartTextInput(this, {screenPosX(), screenPosY(), width(), height()});
	/* If there is a mouse click outside of the edit box - stop editing */
	int mx = psContext->mx;
	int my = psContext->my;
	if (mousePressed(MOUSE_LMB) && !geometry().contains(mx, my))
	{
		StopTextInput(this);
		if (auto lockedScreen = screenPointer.lock())
		{
			lockedScreen->setFocus(nullptr);
		}
		return;
	}

	/* Loop through the characters in the input buffer */
	bool done = false;
	utf_32_char unicode;
	for (unsigned key = inputGetKey(&unicode); key != 0 && !done; key = inputGetKey(&unicode))
	{
		// Don't blink while typing.
		blinkOffset = wzGetTicks();

		int len = 0;

		/* Deal with all the control keys, assume anything else is a printable character */
		switch (key)
		{
		case INPBUF_LEFT :
			/* Move the cursor left */
			insPos = MAX(insPos - 1, 0);

			/* If the cursor has gone off the left of the edit box,
			 * need to update the printable text.
			 */
			if (insPos < printStart)
			{
				printStart = MAX(printStart - WEDB_CHARJUMP, 0);
				fitStringStart();
			}
			debug(LOG_INPUT, "EditBox cursor left");
			break;
		case INPBUF_RIGHT :
			/* Move the cursor right */
			len = aText.length();
			insPos = MIN(insPos + 1, len);

			/* If the cursor has gone off the right of the edit box,
			 * need to update the printable text.
			 */
			if (insPos > printStart + printChars)
			{
				printStart = MIN(printStart + WEDB_CHARJUMP, len - 1);
				fitStringStart();
			}
			debug(LOG_INPUT, "EditBox cursor right (%d, %d, %d)", insPos, printStart, printChars);
			break;
		case INPBUF_UP :
			debug(LOG_INPUT, "EditBox cursor up");
			break;
		case INPBUF_DOWN :
			debug(LOG_INPUT, "EditBox cursor down");
			break;
		case INPBUF_HOME :
			/* Move the cursor to the start of the buffer */
			insPos = 0;
			printStart = 0;
			fitStringStart();
			debug(LOG_INPUT, "EditBox cursor home");
			break;
		case INPBUF_END :
			/* Move the cursor to the end of the buffer */
			insPos = aText.length();
			if (insPos != printStart + printChars)
			{
				fitStringEnd();
			}
			debug(LOG_INPUT, "EditBox cursor end");
			break;
		case INPBUF_INS :
			if (editState == WEDBS_INSERT)
			{
				editState = WEDBS_OVER;
			}
			else
			{
				editState = WEDBS_INSERT;
			}
			debug(LOG_INPUT, "EditBox cursor insert");
			break;
		case INPBUF_DEL :
			delCharRight();

			/* Update the printable text */
			fitStringStart();
			debug(LOG_INPUT, "EditBox cursor delete");
			break;
		case INPBUF_PGUP :
			debug(LOG_INPUT, "EditBox cursor page up");
			break;
		case INPBUF_PGDN :
			debug(LOG_INPUT, "EditBox cursor page down");
			break;
		case INPBUF_BKSPACE :
			/* Delete the character to the left of the cursor */
			delCharLeft();

			/* Update the printable text */
			if (insPos <= printStart)
			{
				printStart = MAX(printStart - WEDB_CHARJUMP, 0);
			}
			fitStringStart();
			debug(LOG_INPUT, "EditBox cursor backspace");
			break;
		case INPBUF_TAB :
			debug(LOG_INPUT, "EditBox cursor tab");
			if (onTabHandler)
			{
				if (onTabHandler(*this))
				{
					done = true;
				}
			}
			break;
		case INPBUF_CR :
		case KEY_KPENTER:					// either normal return key || keypad enter
			/* Finish editing */
			StopTextInput(this);
			if (onRetHandler)
			{
				onRetHandler(*this);
			}
			if (auto lockedScreen = screenPointer.lock())
			{
				lockedScreen->setFocus(nullptr);
			}
			stopEditing();
			debug(LOG_INPUT, "EditBox cursor return");
			return;
			break;
		case INPBUF_ESC :
			debug(LOG_INPUT, "EditBox cursor escape");
			if (aText.length() > 0)
			{
				// hitting ESC while the editbox contains text clears the text
				aText.clear();
				insPos = 0;
				printStart = 0;
				fitStringStart();
				inputLoseFocus();	// clear the input buffer.
			}
			else
			{
				// hitting ESC while the editbox is empty ends editing mode
				if (auto lockedScreen = screenPointer.lock())
				{
					lockedScreen->setFocus(nullptr);
				}
				inputLoseFocus();	// clear the input buffer.
				stopEditing();
				if (onEscHandler)
				{
					onEscHandler(*this);
				}
				return;
			}
			break;

		default:
			if (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL))
			{
				switch (key)
				{
				case KEY_V:
					aText = wzGetSelection();
					// remove any \r, \n chars
					aText.replace(WzUniCodepoint::fromASCII('\r'), "");
					aText.replace(WzUniCodepoint::fromASCII('\n'), "");
					// truncate if too long
					if (aText.length() >= maxStringSize)
					{
						aText.truncate(maxStringSize);
					}
					insPos = aText.length();
					/* Update the printable text */
					fitStringEnd();
					debug(LOG_INPUT, "EditBox paste");
					break;
				default:
					break;
				}
				break;
			}
			/* Dealt with everything else this must be a printable character */
			bool changedText = false;
			if (editState == WEDBS_INSERT)
			{
				changedText = insertChar(WzUniCodepoint::fromUTF32(unicode));
			}
			else
			{
				changedText = overwriteChar(WzUniCodepoint::fromUTF32(unicode));
			}
			if (changedText)
			{
				len = aText.length();
				/* Update the printable chars */
				if (insPos == len)
				{
					fitStringEnd();
				}
				else
				{
					fitStringStart();
					if (insPos > printStart + printChars)
					{
						printStart = MIN(printStart + WEDB_CHARJUMP, len - 1);
						if (printStart >= len)
						{
							fitStringStart();
						}
					}
				}
			}
			break;
		}
	}

	/* Store the current widget state */
	state = (state & ~WEDBS_MASK) | editState;
}

WzString W_EDITBOX::getString() const
{
	return aText;
}

/* Set the current string for the edit box */
void W_EDITBOX::setString(WzString string)
{
	aText = string;
	initialise();
	dirty = true;
}

void W_EDITBOX::setPlaceholder(WzString value)
{
	placeholderText = value;
	dirty = true;
}

void W_EDITBOX::setPlaceholderTextColor(optional<PIELIGHT> _fixedPlaceholderTextColor)
{
	fixedPlaceholderTextColor = _fixedPlaceholderTextColor;
}

void W_EDITBOX::simulateClick(W_CONTEXT *psContext, bool silenceClickAudio /*= false*/, WIDGET_KEY key /*= WKEY_PRIMARY*/)
{
	if (silenceClickAudio)
	{
		suppressAudioCallback = true;
	}
	clicked(psContext, key);
	if (silenceClickAudio)
	{
		suppressAudioCallback = false;
	}
}

/* Respond to a mouse click */
void W_EDITBOX::clicked(W_CONTEXT *psContext, WIDGET_KEY)
{
	if (state & WEDBS_DISABLE)  // disabled button.
	{
		return;
	}

	// Set cursor position to the click location.
	setCursorPosPixels(psContext->mx - x());

	// Cursor should be visible instantly.
	blinkOffset = wzGetTicks();

	if ((state & WEDBS_MASK) == WEDBS_FIXED)
	{
		if (AudioCallback && !suppressAudioCallback)
		{
			AudioCallback(ClickedAudioID);
		}

		/* Set up the widget state */
		state = (state & ~WEDBS_MASK) | WEDBS_INSERT;

		/* Calculate how much of the string can appear in the box */
		fitStringEnd();
		if (printStart > 0)
		{
			insPos = aText.length();
		}

		/* Clear the input buffer */
		inputClearBuffer();

		/* Tell the form that the edit box has focus */
		if (auto lockedScreen = screenPointer.lock())
		{
			lockedScreen->setFocus(shared_from_this());
		}
		else
		{
			// If the W_EDITBOX isn't currently attached to a screen when this is triggered, focus issues may occur
			ASSERT(false, "W_EDITBOX is not attached to any screen?");
		}
	}
	dirty = true;
}


void W_EDITBOX::stopEditing()
{
	if (state & WEDBS_DISABLE)  // disabled button.
	{
		return;
	}

	/* Note the edit state */
	unsigned editState = state & WEDBS_MASK;

	/* Only have anything to do if the widget is being edited */
	if ((editState & WEDBS_MASK) == WEDBS_FIXED)
	{
		return;
	}

	/* Stop editing the widget */
	state = WEDBS_FIXED;
	printStart = 0;
	fitStringStart();
	StopTextInput(this);
	dirty = true;
	if (onEditingStoppedHandler)
	{
		onEditingStoppedHandler(*this);
	}
}

bool W_EDITBOX::isEditing()
{
	unsigned editState = state & WEDBS_MASK;
	return (editState & WEDBS_MASK) != WEDBS_FIXED;
}

/* Respond to loss of focus */
void W_EDITBOX::focusLost()
{
	ASSERT(!(state & WEDBS_DISABLE), "editBoxFocusLost: disabled edit box");

	stopEditing();

	if (auto lockedScreen = screenPointer.lock())
	{
		lockedScreen->setReturn(shared_from_this());
	}
}

/* Respond to a mouse moving over an edit box */
void W_EDITBOX::highlight(W_CONTEXT *)
{
	W_EDITBOX *psWidget = this;
	if (psWidget->state & WEDBS_DISABLE)
	{
		return;
	}

	if (psWidget->AudioCallback)
	{
		psWidget->AudioCallback(psWidget->HilightAudioID);
	}

	psWidget->state |= WEDBS_HILITE;
}


/* Respond to the mouse moving off an edit box */
void W_EDITBOX::highlightLost()
{
	W_EDITBOX *psWidget = this;
	if (psWidget->state & WEDBS_DISABLE)
	{
		return;
	}

	psWidget->state = psWidget->state & WEDBS_MASK;
}

void W_EDITBOX::setTip(std::string string)
{
	pTip = string;
}

void W_EDITBOX::setBoxColours(PIELIGHT first, PIELIGHT second, PIELIGHT background)
{
	boxColourFirst = first;
	boxColourSecond = second;
	boxColourBackground = background;
}

void W_EDITBOX::display(int xOffset, int yOffset)
{
	int x0 = x() + xOffset;
	int y0 = y() + yOffset;
	int x1 = x0 + width();
	int y1 = y0 + height();

	if (pBoxDisplay != nullptr)
	{
		pBoxDisplay(this, xOffset, yOffset);
	}
	else
	{
		iV_ShadowBox(x0, y0, x1, y1, 0, boxColourFirst, boxColourSecond, boxColourBackground);
	}

	/* If there is more text than will fit into the box, display the bit with the cursor in it */
	WzString displayedText = aText;
	displayedText.remove(0, printStart);  // Erase anything there isn't room to display.
	displayedText.remove(printChars, displayedText.length());

	PIELIGHT displayedTextColor = WZCOL_FORM_TEXT;
	if (aText.isEmpty() && !placeholderText.isEmpty())
	{
		displayCache.wzDisplayedText.setText(placeholderText, FontID);
		displayedTextColor = fixedPlaceholderTextColor.value_or((state & WEDBS_MASK) == WEDBS_FIXED ? WZCOL_FORM_TEXT : WZCOL_GREY);
	}
	else
	{
		displayCache.wzDisplayedText.setText(displayedText, FontID);
	}
	if (state & WEDBS_DISABLE)
	{
		displayedTextColor = WZCOL_TEXT_DARK;
	}

	int lineSize = displayCache.wzDisplayedText.lineSize();
	int aboveBase = displayCache.wzDisplayedText.aboveBase();
	int belowBase = displayCache.wzDisplayedText.belowBase();

	int fx = x0 + WEDB_XGAP;// + (psEdBox->width - fw) / 2;
	int fy = y0 + (height() - lineSize) / 2 - aboveBase;

	displayCache.wzDisplayedText.render(fx, fy, displayedTextColor);

	// Display the cursor if editing
	if (((wzGetTicks() - blinkOffset) / WEDB_BLINKRATE) % 2 == 0)
	{
		auto visibleTextBeforeCursor = aText.substr(printStart, insPos - printStart);
		displayCache.modeText.setText(visibleTextBeforeCursor, FontID);
		int cursorX = x0 + WEDB_XGAP + displayCache.modeText.width();
		int cursorY = fy;

		if ((state & WEDBS_MASK) == WEDBS_INSERT)
		{
			// insert mode
			iV_Line(cursorX, cursorY + aboveBase, cursorX, cursorY - belowBase, WZCOL_FORM_CURSOR);
		}
		else if ((state & WEDBS_MASK) == WEDBS_OVER)
		{
			iV_Line(cursorX, cursorY, cursorX + WEDB_CURSORSIZE, cursorY, WZCOL_FORM_CURSOR);
		}
	}

	if (pBoxDisplay == nullptr)
	{
		if ((state & WEDBS_HILITE) != 0)
		{
			/* Display the button hilite */
			iV_Box(x0 - 2, y0 - 2, x1 + 2, y1 + 2, WZCOL_FORM_HILITE);
		}
	}
}

void W_EDITBOX::setMaxStringSize(int size)
{
	maxStringSize = size;
}

void W_EDITBOX::setState(unsigned newState)
{
	unsigned mask = WEDBS_DISABLE;
	state = (state & ~mask) | (newState & mask);
}

void W_EDITBOX::setOnReturnHandler(const OnReturnHandler& func)
{
	onRetHandler = func;
}

void W_EDITBOX::setOnTabHandler(const OnTabHandler& func)
{
	onTabHandler = func;
}

void W_EDITBOX::setOnEscapeHandler(const OnReturnHandler& func)
{
	onEscHandler = func;
}

void W_EDITBOX::setOnEditingStoppedHandler(const OnReturnHandler& func)
{
	onEditingStoppedHandler = func;
}
