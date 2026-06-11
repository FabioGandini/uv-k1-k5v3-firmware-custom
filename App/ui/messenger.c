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
	
	static char String[37];

	memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
	memset(String, 0, sizeof(String));

	//UI_PrintStringSmallBold("MESSENGER", 0, 127, 0);
	UI_PrintStringSmallNormal("Messenger", 1, 127, 0);

	UI_DrawLineDottedBuffer(gFrameBuffer, 2, 3, 26, 3, true);
	UI_DrawLineDottedBuffer(gFrameBuffer, 100, 3, 126, 3, true);

	/*if ( msgStatus == SENDING ) {
		GUI_DisplaySmallest("SENDING", 100, 6, false, true);
	} else if ( msgStatus == RECEIVING ) {
		GUI_DisplaySmallest("RECEIVING", 100, 6, false, true);
	} else {
		GUI_DisplaySmallest("READY", 100, 6, false, true);
	}*/

	// RX Screen

	//GUI_DisplaySmallest("RX", 4, 34, false, true);

	memset(String, 0, sizeof(String));
	
	uint8_t mPos = 8;
	const uint8_t mLine = 7;
	for (int i = 0; i < 4; ++i) {
		//sprintf(String, "%s", rxMessage[i]);
		GUI_DisplaySmallest(rxMessage[i], 2, mPos, false, true);
		mPos += mLine;
    }

	// TX Screen
	
	UI_DrawLineDottedBuffer(gFrameBuffer, 14, 40, 126, 40, true);
	memset(String, 0, sizeof(String));
	if ( keyboardType == NUMERIC ) {
		strcpy(String, "2");
	} else if ( keyboardType == UPPERCASE ) {		
		strcpy(String, "B");
	} else {		
		strcpy(String, "b");
	}

	UI_DrawRectangleBuffer(gFrameBuffer, 2, 36, 10, 44, true);
	GUI_DisplaySmallest(String, 5, 38, false, true);

	memset(String, 0, sizeof(String));
	sprintf(String, "%s_", cMessage);
	//UI_PrintStringSmallNormal(String, 3, 0, 6);
	GUI_DisplaySmallest(String, 5, 48, false, true);

	// debug msg: live FSK register dump (no UART needed)
	// R = MsgRX setting, Y/E = sync/finished interrupt counters, then the
	// actual chip state: REG_3F (irq mask), REG_58 (FSK mode/enable),
	// REG_59 (RX enable bit12), REG_70 (Tone2 gain)
	// NOTE: gFrameBuffer is only 7 rows tall (y must be 0-55), so this
	// must fit on the single free row at y=42 (x>=14)
	memset(String, 0, sizeof(String));
	sprintf(String, "R%uY%uE%u %04X %04X %04X %04X",
		gEeprom.MESSENGER_CONFIG.data.receive,
		gMsgDebugSyncCount, gMsgDebugFinishedCount,
		BK4819_ReadRegister(BK4819_REG_3F),
		BK4819_ReadRegister(BK4819_REG_58),
		BK4819_ReadRegister(BK4819_REG_59),
		BK4819_ReadRegister(BK4819_REG_70));
	GUI_DisplaySmallest(String, 14, 42, false, true);

	ST7565_BlitFullScreen();
}

#endif