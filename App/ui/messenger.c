/* Original work Copyright 2023 joaquimorg
 * https://github.com/joaquimorg
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifdef ENABLE_MESSENGER

#include <string.h>

#include <string.h>
#include "app/messenger.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "functions.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/messenger.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

void UI_DisplayMSG(void) {
	
	memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

	// header (page 0)
	UI_PrintStringSmallNormal(gMsgEditCallsign ? "Set Callsign" : "Messenger", 1, 127, 0);
	UI_DrawLineDottedBuffer(gFrameBuffer, 2, 3, 26, 3, true);
	UI_DrawLineDottedBuffer(gFrameBuffer, 100, 3, 126, 3, true);

	// message history: MSG_VISIBLE_LINES lines in the bigger font, scrollable
	// with UP/DOWN (gMsgScroll). Each line is truncated to the display width so
	// it can never overflow past the 128px framebuffer row.
	{
		char line[19];
		int top = (int)(MSG_LINES - MSG_VISIBLE_LINES) - (int)gMsgScroll;
		for (uint8_t row = 0; row < MSG_VISIBLE_LINES; row++) {
			int idx = top + (int)row;
			if (idx < 0 || idx >= MSG_LINES || rxMessage[idx][0] == 0)
				continue;
			strncpy(line, rxMessage[idx], 18);
			line[18] = 0;
			UI_PrintStringSmallNormal(line, 1, 0, 1 + row);  // left-aligned (End=0)
		}
	}

	// "scrolled back" marker
	if (gMsgScroll > 0)
		GUI_DisplaySmallest("^", 123, 9, false, true);

	// divider above the compose line (page 5 / y40)
	UI_DrawLineDottedBuffer(gFrameBuffer, 0, 40, 127, 40, true);

	// keyboard-type indicator (B / b / 2)
	{
		const char *kb = (keyboardType == NUMERIC) ? "2" :
		                 (keyboardType == UPPERCASE) ? "B" : "b";
		GUI_DisplaySmallest(kb, 3, 50, false, true);
	}

	// compose line (page 6), showing the tail so the cursor stays in view
	{
		char inp[20];
		size_t L = strlen(cMessage);
		const char *src = (L > 17) ? cMessage + (L - 17) : cMessage;
		snprintf(inp, sizeof(inp), "%s_", src);
		UI_PrintStringSmallNormal(inp, 12, 0, 6);  // left-aligned (End=0)
	}

	ST7565_BlitFullScreen();
}

#endif